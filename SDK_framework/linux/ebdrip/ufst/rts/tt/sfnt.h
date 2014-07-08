/* $HopeName: GGEufst5!rts:tt:sfnt.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:tt:sfnt.h,v 1.2.10.1.1.1 2013/12/19 11:24:05 rogerb Exp $ */
/* Log stripped */
/* $Date: 2013/12/19 11:24:05 $ */
/*
   File:    sfnt.h


   Copyright: © 1988-1990 by Apple Computer, Inc., all rights reserved.

    Change History (most recent first):

    AGFA changes:
	23-Jan-02  jfd  Renamed "nstk" tag to lowercase.
	14-Jan-02  jfd  Added Stik font support.
    14-Apr-00  keb  Added sfnt_VerticalHeader and sfnt_VerticalMetrics to
                    support vertical writing
    10-Nov-99  slg	Added TrueTypeCollection header table 'TTC_HeaderTable';
    				deleted unused typedefs and defines
    01-Jan-97  dlk  Added 'sfnt_NamingTbl' to sfnt_tableIndex enum type -
                    to follow work by dbk on ASIAN_ENCODING.
    19-Nov-96  mby  Added "sfnt_name" to sfnt_tableIndex enum type.
    22-Jul-92  mby  Added constants for access to sfnt_DirectoryEntry and
                    sfnt_OffsetTable data.

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		 <5+> 7/11/91  CKL    Added standard header conditionals
		 <5>	12/20/90	MR		Add caretOffset to horiHeader (replacing reserved0) [rb]
		 <4>	12/11/90	MR		Add use-my-metrics support for devMetrics in component glyphs.
									[rb]
		 <3>	10/31/90	MR		Add bit-field option for integer or fractional scaling [rb]
		 <2>	10/20/90	MR		Remove unneeded tables from sfnt_tableIndex. [rb]
		<12>	 7/18/90	MR		platform and specific should always be unsigned
		<11>	 7/14/90	MR		removed duplicate definitions of int[8,16,32] etc.
		<10>	 7/13/90	MR		Minor type changes, for Ansi-C
		 <9>	 6/29/90	RB		revise postscriptinfo struct
		 <7>	  6/4/90	MR		Remove MVT
		 <6>	  6/1/90	MR		pad postscriptinfo to long word aligned
		 <5>	 5/15/90	MR		Add definition of PostScript table
		 <4>	  5/3/90	RB		mrr		Added tag for font program 'fpgm'
		 <3>	 3/20/90	CL		chucked old change comments from EASE
		 <2>	 2/27/90	CL		getting bbs headers
	   <3.1>	11/14/89	CEL		Instructions are legal in components.
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
		<3+>	 3/20/90	mrr		Added tag for font program 'fpgm'
*/

#ifndef __SFNT__
#define __SFNT__

#ifndef __SFNT_ENUM__
#include "sfntenum.h"
#endif

typedef struct {
	UL32 bc;
	UL32 ad;
} BigDate;

typedef struct {
	sfnt_TableTag	tag;
	UL32			checkSum;
    UL32			offset;
	UL32			length;
} sfnt_DirectoryEntry;

/*
 *	The search fields limits numOffsets to 4096.
 */
typedef struct {
	SL32 version;					/* 0x10000 (1.0) */
	UW16 numOffsets;				/* number of tables */
	UW16 searchRange;				/* (max2 <= numOffsets)*16 */
	UW16 entrySelector;			/* log2(max2 <= numOffsets) */
	UW16 rangeShift;				/* numOffsets*16-searchRange*/
	sfnt_DirectoryEntry table[1];	/* table[numOffsets] */
} sfnt_OffsetTable;

/* Offset table and Directory entry constants */
#define OFFSETTABLESIZE   12
#define SFNTD_NUMTABLES    4
#define SFNTD_TAG          0
#define SFNTD_CHECKSUM     4
#define SFNTD_OFFSET       8
#define SFNTD_LENGTH      12
#define SFNTD_TABLESIZE   16

#define TTC_TAG			0
#define TTC_NUMFONTS	8
#define TTC_OFFSET		12

/* TTC (TrueType Collection) Header - at start of every TTC file */
/* There will be "numFonts" TrueType fonts in the file: "table[numFonts]"
	gives the offsets to the TableDirectories for each font in the file -
	the TableDirectory and table formats are the same as for TTF files. */

typedef struct {
	sfnt_TableTag	tag;			/* 'ttcf' */
	Fixed			version;		/* 0x10000 (1.0) */
    UL32			numFonts;		/* number of fonts in TTC file */
	/* sfnt_OffsetTable table[1]; */		/* table[numFonts] */
} TTC_HeaderTable;

/*
 *	for the flags field
 */
#define USE_INTEGER_SCALING		0x0008

#define SFNT_MAGIC 0x5F0F3CF5L

#define SHORT_INDEX_TO_LOC_FORMAT		0
#define LONG_INDEX_TO_LOC_FORMAT		1

typedef struct {
    Fixed		version;			/* for this table, set to 1.0 */
    Fixed		fontRevision;		/* For Font Manufacturer */
	UL32		checkSumAdjustment;
	UL32		magicNumber; 		/* signature, should always be 0x5F0F3CF5  == MAGIC */
	UW16		flags;
	UW16		unitsPerEm;			/* Specifies how many in Font Units we have per EM */

	BigDate		created;
	BigDate		modified;

	/** This is the font wide bounding box in ideal space
	(baselines and metrics are NOT worked into these numbers) **/
	FWord		xMin;
	FWord		yMin;
	FWord		xMax;
	FWord		yMax;

	UW16		macStyle;				/* macintosh style word */
	UW16		lowestRecPPEM; 			/* lowest recommended pixels per Em */

	/* 0: fully mixed directional glyphs, 1: only strongly L->R or T->B glyphs, 
	   -1: only strongly R->L or B->T glyphs, 2: like 1 but also contains neutrals,
	   -2: like -1 but also contains neutrals */
	SW16		fontDirectionHint;

	SW16		indexToLocFormat;
	SW16		glyphDataFormat;
} sfnt_FontHeader;

typedef struct {
	Fixed		version;				/* for this table, set to 1.0 */

	FWord		yAscender;
	FWord		yDescender;
	FWord		yLineGap;		/* Recommended linespacing = ascender - descender + linegap */
	uFWord		advanceWidthMax;	
	FWord		minLeftSideBearing;
	FWord		minRightSideBearing;
	FWord		xMaxExtent; /* Max of ( LSBi + (XMAXi - XMINi) ), i loops through all glyphs */

	SW16		horizontalCaretSlopeNumerator;
	SW16		horizontalCaretSlopeDenominator;

	FWord		caretOffset;
	UW16		reserved1;
	UW16		reserved2;
	UW16		reserved3;
	UW16		reserved4;

	SW16		metricDataFormat;			/* set to 0 for current format */
	UW16		numberOf_LongHorMetrics;	/* if format == 0 */
} sfnt_HorizontalHeader;

typedef struct {
	Fixed		version;				/* for this table, set to 1.0 */
	UW16		numGlyphs;
	UW16		maxPoints;				/* in an individual glyph */
	UW16		maxContours;			/* in an individual glyph */
	UW16		maxCompositePoints;		/* in an composite glyph */
	UW16		maxCompositeContours;	/* in an composite glyph */
	UW16		maxElements;			/* set to 2, or 1 if no twilightzone points */
	UW16		maxTwilightPoints;		/* max points in element zero */
	UW16		maxStorage;				/* max number of storage locations */
	UW16		maxFunctionDefs;		/* max number of FDEFs in any preprogram */
	UW16		maxInstructionDefs;		/* max number of IDEFs in any preprogram */
	UW16		maxStackElements;		/* max number of stack elements for any individual glyph */
	UW16		maxSizeOfInstructions;	/* max size in bytes for any individual glyph */
	UW16		maxComponentElements;	/* number of glyphs referenced at top level */
	UW16		maxComponentDepth;		/* levels of recursion, 1 for simple components */
} sfnt_maxProfileTable;


/* keb 4/00 */
typedef struct  {
    Fixed		version;				/* for this table, set to 1.0 */

	FWord		xAscender;
	FWord		xDescender;
	FWord		xLineGap;		/* Recommended linespacing = ascender - descender + linegap */
	uFWord		advanceHeightMax;	
	FWord		minTopSideBearing;
	FWord		minBottomSideBearing;
	FWord		yMaxExtent; /* Max of ( TSBi + (YMAXi - YMINi) ), i loops through all glyphs */

	SW16		verticalCaretSlopeNumerator;
	SW16		verticalCaretSlopeDenominator;

	FWord		caretOffset;
	UW16		reserved1;
	UW16		reserved2;
	UW16		reserved3;
	UW16		reserved4;

	SW16		metricDataFormat;			/* set to 0 for current format */
	UW16		numberOf_LongVerMetrics;	/* if format == 0 */
} sfnt_VerticalHeader;


/* #if GET_VERTICAL_METRICS		08-20-04 qwu */ 
typedef struct {
	UW16		advanceHeight;
    SW16 		topSideBearing;
} sfnt_VerticalMetrics;
/* #endif		08-20-04 qwu */

/*
 *	CVT is just a bunch of SW16s
 */
typedef SW16 sfnt_ControlValue;

/*
 *	Char2Index structures, including platform IDs
 */

typedef struct {
	UW16	platformID;
	UW16	specificID;
	UL32	offset;
} sfnt_platformEntry;

typedef struct {
	UW16	version;
	UW16	numTables;
	sfnt_platformEntry platform[1];	/* platform[numTables] */
} sfnt_char2IndexDirectory;

#define STUBCONTROL 0x10000L
#define NODOCONTROL 0x20000L

typedef struct {
	SL32 	*x, *y;
	SL32	*ox, *oy;	/* for new fs_FindCharBearings code */
	SW16	*sp, *ep;
	SW16 	ctrs;
	SB8 	*onC;
} sc_CharDataType;

/* Internal flags for the onCurve array */
#define OVERLAP 0x02 /* can not be the same as ONCURVE in sfnt.h */

/*
 * UNPACKING Constants
*/
#define ONCURVE  			0x01
#define XSHORT   			0x02
#define YSHORT   			0x04
#define REPEAT_FLAGS    	0x08 /* repeat flag n times */
/* IF XSHORT */
#define SHORT_X_IS_POS   	0x10 /* the short vector is positive */
/* ELSE */
#define NEXT_X_IS_ZERO   	0x10 /* the relative x coordinate is zero */
/* ENDIF */
/* IF YSHORT */
#define SHORT_Y_IS_POS   	0x20 /* the short vector is positive */
/* ELSE */
#define NEXT_Y_IS_ZERO   	0x20 /* the relative y coordinate is zero */
/* ENDIF */
/* 0x40 & 0x80				RESERVED
** Set to Zero
**
*/

/*
 * Composite glyph constants
 */
#define ARG_1_AND_2_ARE_WORDS		0x0001	/* if set args are words otherwise they are bytes */
#define ARGS_ARE_XY_VALUES			0x0002	/* if set args are xy values, otherwise they are points */
#define ROUND_XY_TO_GRID			0x0004	/* for the xy values if above is true */
#define WE_HAVE_A_SCALE				0x0008	/* Sx = Sy, otherwise scale == 1.0 */
#define NON_OVERLAPPING				0x0010	/* set to same value for all components */
#define MORE_COMPONENTS				0x0020	/* indicates at least one more glyph after this one */
#define WE_HAVE_AN_X_AND_Y_SCALE	0x0040	/* Sx, Sy */
#define WE_HAVE_A_TWO_BY_TWO		0x0080	/* t00, t01, t10, t11 */
#define WE_HAVE_INSTRUCTIONS		0x0100	/* instructions follow */
#define USE_MY_METRICS				0x0200	/* apply these metrics to parent glyph */

typedef enum {
	name_Copyright,
	name_Family,
	name_Subfamily,
	name_UniqueName,
	name_FullName,
	name_Version,
	name_Postscript,
	name_Trademark
} sfnt_NameIndex;

/* defines for NAME table access */

#define NAME_NUM_REC       2
#define NAME_OFF_TO_STRS   4
#define NAME_NAMERECS      6
#define NAME_SIZE_NAMEREC  12
#define NAME_TAB_PLATID    0
#define NAME_TAB_SPECID    2
#define NAME_TAB_LANGID    4
#define NAME_TAB_NAMEID    6
#define NAME_TAB_STRLEN    8
#define NAME_TAB_STROFF    10

/* defines for HHEA table access */

#define HHEA_ASCENDER      4

/* defines for OS/2 table access */

#define OS2_FSTYPE			8
#define OS2_STYPOASCENDER  68
#define OS2_STYPODESCENDER 70
#define OS2_STYPOLINEGAP   72
#define OS2_USWINASCENT    74
#define OS2_USWINDESCENT   76

#define OS2_FAMILYCLASS 30
#define OS2_FSSELECTION 62
#define SFLAGBOLD       0x20
#define SFLAGITALIC     0x01

#define BUCKBITBOLD     0x01
#define BUCKBITNOSERIF  0x02
#define BUCKBITITALIC   0x04
#define BUCKBITNOFP     0x08
#define BUCKBITCOUR     0x10
#define BUCKBITLGOT     0x20

/* defines for PCLT table access */

#define PCLT_STYLELSB   13
#define PCLT_STROKEWT   50
#define PCLT_SERIFSTYLE 52

/* defines for POST table access */

#define POST_FIXEDPITCH 12
#define POST_HEADERSIZE 32	/* size of "post" header data */

/*
 *	Private enums for tables used by the scaler.  See sfnt_Classify
 */
typedef enum {
	sfnt_fontHeader,
	sfnt_horiHeader,
	sfnt_indexToLoc,
	sfnt_maxProfile,
	sfnt_controlValue,
	sfnt_preProgram,
	sfnt_horizontalMetrics,
	sfnt_charToIndexMap,
	sfnt_fontProgram,
    sfnt_OS_2,
    sfnt_PostScript,
    sfnt_PCLT,
    sfnt_name,
	sfnt_vertHeader,
	sfnt_verticalMetrics, /* keb 4/00 */
	sfnt_nstk,				/* 12-06-01 jfd */
	sfnt_NUMTABLEINDEX
} sfnt_tableIndex;

#endif  /*  __SFNT__  */

/* -------------------------------------------------------------------
                            END OF "sfnt.h"
   ------------------------------------------------------------------- */

