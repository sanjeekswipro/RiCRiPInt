
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* fc_da.h */


/*-----------------------------------------------------------------*/
 
#ifndef __FC_DA__
#define __FC_DA__

/* ---------------------------- TABLETYPE----------------------------*/
typedef struct
{
    DAFILE* f;   /* font collection file */
    SL32  key;
    SL32 start;      /* offset from start of font collection to table  */
    SL32 len;        /* length of table in bytes                       */
    SL32  numEntries; /* number of entries in the table                 */
    SL32 tableStart; /* offset from start of file to table start       */
} TABLETYPE;

/* Methods: Private: see fc_da.c
 *   UW16 FCtableNew   (TABLETYPE*, DAFILE*, SL32, SL32*);
 *   VOID FCtableClose (TABLETYPE*);
 *   UW16 FCtableOffset(TABLETYPE*, SL32, SL32*);
 */

/* --------------------------TTFONTINFOTYPE--------------------------*/
typedef struct
{
    UL32   scaleFactor;    /* design units per em */
    UL32   spaceBand;      /* width in design units = pitch */
    UL32   fixedSpaceThin; /* fixed space widths in design units */
    UL32   fixedSpaceEN;
    UL32   fixedSpaceEM;
    SL32            baselinePosition; /* 0 always */
    SL32            ascender;
    SL32            descender;
    SL32            capHeight;
    SL32            xHeight;
    SL32            isFixedPitch;   /* 'post' table: 0=proportional; 1=F.P. */
    SL32            italicAngle;    /*   tangent of angle * 32768.0 */
    SL32            uScoreDepth;    /*   underscore position */
    SL32            uScoreThickness;/*   and thickness */
    UL32  pcltFontNumber; /* PCLT table */
    UW16 pcltStyle;
    UW16 pcltTypeFamily;
    UB8  pcltChComp[8];
    UW16 pcltTypeface;   /* index into String Table */
    UW16 pcltFileName;   /* index into String Table */
    SB8           pcltStrokeWt;
    UB8           pcltWidthType;
    UB8  pcltSerifStyle;
    UW16 usWeightClass;  /* OS/2 table */
    UW16 usWidthClass;
    UB8  panose[10];
/* Strings for PCLT and 'name' tables */
    UW16 tfName;         /* index into String Table */
    UW16 familyName;     /* index into String Table */
    UW16 weightName;     /* index into String Table */
    UW16 tfDescriptor;   /* index into String Table */
    UW16 copyrightNotice;/* index into String Table */
    SW16          sFamilyClass;
    UW16 fsSelection;
    UW16 orThreshold;
    UW16 standardCutin;  /* in pixelsize units (inverse of pointsize) */
    UW16 stanStanCutin;
    UW16 pluginDescriptor;
    SW16          FontBBoxXmin;   /* Added for FCO format V11 */
    SW16          FontBBoxXmax;
	/*keb*/
    SW16          FontBBoxYmin;
	SW16          FontBBoxYmax;
    UW16 usWinAscent;   /* OS/2 table */
    UW16 usWinDescent;         
    SL32            LineGap;       /* hhea table */
    UW16 sTypoLineGap;  /* OS/2 table */
	UW16 psname;

} TTFONTINFOTYPE;


/* ---------------------------- FCTYPE -----------------------------*/

#define VERSION_12     0x12    /* Minimum supported revision number */
#define VERSION_13     0x13
#define VERSION_14     0x14
#define VERSION_15     0x15

/* Current version number */
#if FCO3
#define VERSION_NUMBER VERSION_15
#endif
#if FCO2
#define VERSION_NUMBER VERSION_14
#endif
#if FCO1
#define VERSION_NUMBER VERSION_13
#endif

typedef struct
    {
    MEM_HANDLE encodeKeyH;
    MEM_HANDLE basisH;
    MEM_HANDLE rangeH;
    }
    INTERMED_TYPE;

typedef struct
    {
    MEM_HANDLE escapementsH;    /* escapement values */
    UB8 numEscBits;   /* num of bits required for escapement list index */
    }
    ESCAPEMENT_TYPE;

#if FCO_ROM
typedef struct
{
    UB8*  strings;    /* if ROM, set pointers into the FCO */
    UB8*  postStrings;
    UB8*  yClassData;
} ROM_PTRS;
#endif

#if FCO_DISK
typedef struct
{
    MEM_HANDLE      strings;    /* if DISK, allocate memory, then read from file */
    MEM_HANDLE      postStrings;
    MEM_HANDLE      yClassData;
} DISK_HNDLS;
#endif

typedef struct
{
#if FCO_ROM
    ROM_PTRS r;
#endif
#if FCO_DISK
    DISK_HNDLS d;
#endif
} FC_ACCESS;

typedef struct
{
    DAFILE f;
    SL32   error;
    SL32   version;
    SL32  len;
    SL32   numTables;

    TABLETYPE modelGroups;
    TABLETYPE charMaps;
    TABLETYPE fonts;
    TABLETYPE compCharDefs;
    TABLETYPE pluginTree;
    TABLETYPE CurveData;

    SL32   FCWstanXstem;
    SL32   FCWstanXwidth;
    SL32   FCWstanYstem;
    SL32   FCWstanYwidth;
    SL32   numCharGroups;
    SL32   numYdimens;           /* sum of Ydimen values in "charGroupOffsH" */
    SL32   numXdimens;           /* sum of Xdimen values in "charGroupOffsH" */
    MEM_HANDLE charGroupOffsH;  /* (SW16*) array of ydim, then xdim offsets */
    MEM_HANDLE dimTypesH;       /* FCO-wide data: STEM/WIDTH flags; Xproj flags */
    SW16 XdimProjTypeOff;      /* offset to X dim projection flags */
	SL32		CurveDataFlag;					/* Use as a global flag to indicate presence of Curve Data Seg */
	SL32		numCbkClasses;					/* num of codebook classes. */
	SL32		numCbkTotal;					/* sum of number of codebooks per class (i.e. total num of cbks) */
	MEM_HANDLE CbkClassOffsH;				/* (SW16 *) array of codebook offsets for each class */
	SL32       CbkDimensionOff;				/* (char  *) array of codebook vector dimension class */
	                                         /*  offset to the end of charGroupOffsH */
    SL32   maxPoints;            /* max nr points of any character in FCO */
    SL32   maxAssocs;            /* max number of associations of any character in FCO */
    

    FC_ACCESS access;

} FCTYPE;

typedef struct
{
    TTFONTINFOTYPE tfDesc;
    FC_ACCESS access;

} FCACCESSTYPE;


/* ---------------------------- CHMAPTYPE -----------------------------*/

typedef struct
{
    MEM_HANDLE   mapTablesH;     /* handle to Char Map Tables; pointer to
                                    map for simple chars */
    SW16        mapTabAccOff;   /* word offset to accent CC map   */
    SW16        mapTabAccExcOff; /* word offset to accent exception map */
    SW16        mapTabCplxOff;  /* word offset to complex CC map  */
    SW16        mapTabUniOff;   /* word offset to partUnicode map */
    UL32 numSimpleChars;
    UL32 numAccentCC;
    UL32 numAccentExcs;
    UL32 numComplexCC;
    UL32 numParts;
    UL32 chmapFormatPS;  /* 0 if PS name table is absent, 1 if present (see chmapLookup(), chmapNew()) */
    UB8         pclt[8];
} CHMAPTYPE;

/* Methods: Private: see fc_da.c
 *   UW16 FCchmapNew   (CHMAPTYPE*, SL32, FCTYPE*);
 *   VOID FCchmapClose (CHMAPTYPE*);
 *   UW16 FCchmapLookup(FONTTYPE*, UL32, UL32*, UL32*, UL32*, UW16*);
 *   UW16 FCchmapValue (CHMAPTYPE*, SL32, UL32, UW16*, UW16*);
 */

/* ------------------------- FONTTYPE -----------------------------*/
#define PRIMARYFLAG  0          /* Passed to FCfontNew() if this is the primary font */
#define CVFFLAG      1          /* Passed to FCfontNew() if this is a converged font */
#define SHAREFLAG    2          /* Passed to FCfontNew() if this font is the source for Curve Sharing */
#define NARROW100PCT 32768L
  /* values of FONTTYPE.cvfFlag */
#define CVF_OB_SC    1          /* Obliquing and/or Scaling to diffr units */
#define CVF_NARROW   2          /* Narrowing */
#define CVF_NW_OB    3          /* Narrowing AND Obliquing */

typedef struct      /* Convergent Font data structure */
{
    MEM_HANDLE  altFontH;       /* handle to alternate Font */
    SW16     fIndex;           /* Index of alternate Font  */
    SW16     italTan;          /* tan(italicAngle) * 32768 */
    UW16  narrowFactor; /*  * 32768 (algorithmic narrowing- range: 0 - 2.0) */
    SW16     globalHorAdj;     /* global hor. adjustment   */
    UW16 numHorAdj;   /* nr of adjs in hor offset table */
    UW16 numProhItal; /* nr entries in prohibit table   */
    MEM_HANDLE     cvHorAdjListH; /* allocate memory to hold hor offset and prohibit tables */
    UW16 cvProhItalOff; /* offset to prohibit arrays */
} CVFTYPE;

typedef struct
{
    DAFILE*   f;
    FCTYPE*   fc;
    SL32       fontIx;
    SL32      startOff;      /* Byte offset from start of font collection */
    SL32      len;           /* Length of font in bytes */
    SL32       headerSize;
    SL32       offsetDescrip; /* Byte offset from start of font to TF desc */
    SL32       charMapIndex;  /* Charmap index for this face */
    CHMAPTYPE chmap;         /* Charmap information */
    UW16  segShareFont; /* Outline sharing font index */
    MEM_HANDLE  shareFontH;  /* handle to new font for curve sharing */
	SL32		CbkListSize;					/* Size of list of MEM_HANDLES pointing to codebooks. Same value as numCbkClasses */
	MEM_HANDLE  CbkIndexH;					/* handle to array of codebook indices */
	MEM_HANDLE  CbkLenexH;					/* handle to array of codebook indices length in bits (power of two of size) */
	MEM_HANDLE  CbkPntrxH;					/* handle to array of codebook indices pointers */
	MEM_HANDLE  CodebookH;					/* if DISKFILE, allocate memory, then read from file, else determine ptrs to ROM */
    INFLATE   inflate;       /* Typeface global data, used in intelliflator() */
    SL32      charIndOffset; /* File offset to start of character index */
    SL32       numExceptions; /* Size of character Exception Table */
    MEM_HANDLE  excTbl_CCinfoH;  /* Handle to Font Exception Table & complex compound char information */
    SW16     cplxCCEsc;     /* Complex CCs: offset to escapement array */
    SW16     cplxCCXYoff;   /* Complex CCs: offset to XY offset array  */
	BOOLEAN	font_access;	/* set by user to identify font access by */
								/* DISK (DISK_ACCESS=0) or ROM (ROM_ACCESS=1) */
    TTFONTINFOTYPE  tfDesc;  /* Typeface global information */
    SL32       cvfFlag;       /* Convergent Font segment flag
                              *   0: not CVF
                              *   1: standard Convergent Font
                              *   2: narrowed Convergent Font (less data) */
    CVFTYPE   cvf;           /* Convergent Font data structure */
    
} FONTTYPE;


/* ------------------------- CHARTYPE -----------------------------*/

#define ATYP_SIMPLE  0x0000     /* Simple characters           */
#define ATYP_COMP1   0x4000     /* Accented compound character */
#define ATYP_COMP2   0x8000     /* Complex compound character  */
#define ATYP_COMP3   0xc000     /* Reserved */
#define SIMP_CHAR    0x0        /* Simple characters           */
#define CC_ACCENT    0x1        /* Accented compound character */
#define CC_COMPLX    0x2        /* Complex compound character  */
#define COMPLXFLAGSMASK  0xFFF
#define COMPLXFLAGSSHIFT 12
#define SPECIALCASEMASK  0x8
#define EXSPECIALCASEMASK  0x7
#define ALTGLYPH     1

typedef struct
{
    UB8  compCharType;
    MEM_HANDLE     compCharDataH;
    UB8* baseChar;
} COMPCHARTYPE;

#define SEGSHAR_MASK 0xC000
#define SEGSHAR_EMASK 0x3FFF
#define SEGSHAR_NO   0x00
#define SEGSHAR_YES  0x40
#define SEGSHAR_MAYB 0x80
#define CONVRG_MASK  0xC0

typedef struct
{
    FONTTYPE*   font;
    UW16 unicode;
    SL32         escapement;    /* escapement of whole character, returned by fco_make_gaso_and_stats(), fco_get_width() */
    SL32         partEscapement; /* escapement of compound char piece, used in "intelliflator" */
    SL32         altEscapement; /* escapement of Convergent Font char */
    UB8 altGlyph;    /* Convergent Fonts: 0 or 1 for set width adj, oblique matrix */
    UB8 segmSharing; /* contour segment sharing bits; converged character flag */
    SL32         modelIndex;
    MEM_HANDLE  charDataH;     /* mem handle to buffer for character data */
    UB8 charDataBitPos; /* bit in charData stream at which local dims begin */
    SW16       locYXdimsOff;  /* offset to local dimensions       */
    SW16       segDescOff;    /* offset to character segment data */
#ifdef AGFADEBUG
#if FCO1
    SW16       segCoordsOff;
#endif  /* FCO1 */
#endif
    SL32         compoundChar;  /* =0 for simple char; =N for compound char with N pieces. */
    COMPCHARTYPE  ccType;
    MODELTYPE   model;
    SL32         stanIndex;
#if IF_RDR
    SL32         IF_extCC;      /* flag for PCLEO external compound char */
    SW16VECTOR  IFextCC_offs;  /* offsets for PCLEO ext CC */
#endif

    MEM_HANDLE  XYdataH;       /* mem handle to buffer for coordinate data */
        /* Memory is allocated in FCcharNew();  */
        /* arrays are filled by intelliflator(), fco_make_gaso_and_stats(). */
    SW16  XsyntellOff;        /* offset to 32-bit x coordinate data */
    SW16  YsyntellOff;        /*  ... y coord. data */
    SW16  IsyntellOff;        /*  ... x coord. data for italics */
    SW16  XintOff;            /* offset to integer x coordinate data */ 
    SW16  YintOff;            /*  ... y coord. data */
    SW16  IintOff;            /*  ... x coord. data for italics */
    UW16 psoffset;   /* index into PostScript strings table */

#if USBOUNDBOX						/* AJ 10-04-04 */
    MEM_HANDLE  USBBOXXYdataH;       /* mem handle to buffer for coordinate data */
#endif

} CHARTYPE;

typedef struct
{
    SL32       partNr;
    CHARTYPE* charData;
    FONTTYPE* fontData;    /* added for External Compound support, 12-15-94 */
    SL32       longXOff;    /* long-form X offset */
    SL32       longYOff;    /* long-form Y offset */
    SL32       longCCFlag;  /* long-form mirror flags: bit0 = rotate 90ø */
} COMPPIECETYPE;           /*          bit1 = mirror X; bit2 = mirror Y */



#define MAX_PTREES   8     /* max nr plugin trees (prop; TT; PS; FP-1; FP-2) */
                           /* jwd, 5/15/07. Increased value to 8, to account */
                           /* for full range value is used.                  */
typedef struct {
    UB8 wspu[15];
} PTREE;

typedef struct {
    SL32    nTrees;
    SB8    treeIx[MAX_PTREES];
    PTREE  tree[MAX_PTREES];
} TREETYPE;


#endif	/* __FC_DA__ */

