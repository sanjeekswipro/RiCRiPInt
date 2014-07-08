/* $HopeName: GGEufst5!rts:fco:fc_da.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:fco:fc_da.h,v 1.3.8.1.1.1 2013/12/19 11:24:04 rogerb Exp $ */
/* $Date: 2013/12/19 11:24:04 $ */

/* fc_da.h */


/*-----------------------------------------------------------------*/

/* History:
 *
 * 14-Mar-94  mby  Changed CHARTYPE.*syntell declaration from float to long.
 *                 Moved declaration of intelliflator() to fc_pixel.h.
 * 24-Mar-94  mby  Removed unused defn of FCreadSegs(). Fixed typo:
 *                 FCversion() => FCnumVersion().
 * 12-Apr-94  mby  Added Isyntell[] and Iint[] to CHARTYPE structure.
 * 03-Jun-94  mby  Added declarations for FCfontEmRes() and FCfontBBox().
 *                 Added duPerEm and fontBBox[4] to FONTTYPE structure.
 * 18-Jul-94  mby  Moved dmemAlloc and dmemFree macros to "fc_dafil.h".
 *                 Added constants for compound character processing.
 * 29-Jul-94  mby  Added 'compCharDefs' to FCTYPE.
 *                 Added 'compoundChar', 'ccType' to CHARTYPE.
 *                 Added COMPCHARTYPE and COMPPIECETYPE structures.
 *                 Added declarations for FCcompCharPiece(), FCcompCharShort()
 * 02-Aug-94  mby  In CHARTYPE, made Xsyntell[], ... and Xint[], ... arrays
 *                 dynamic instead of always [NUM_YX_SKEL].
 *                 Changed CHMAPTYPE.numChars from 'int' to 'unsigned int'.
 *                 In FCcharNew() changed "unicode" from 'int' to 'unsigned int'.
 *                 In declaration of FCchmapLookup(), changed "unicode" and
 *                 "index" parameters from 'int' to 'unsigned int'.
 * 05-Aug-94  mby  Added TTFONTINFOTYPE structure. Declared data access
 *                 function FCfontInfoTT().
 * 22-Aug-94  sbm  Added 'unicode' field to CHARTYPE structure.
 * 25-Aug-94  sbm  Added two new args, anchorIndexBase and anchorIndex,
 *                 to FCcompCharShort.
 * 31-Aug-94  mby  Added 'TABLETYPE pluginTree' to FCTYPE. Added 'TFGLOBAL TFglobal'
 *                 to FONTTYPE. Added TFGLOBAL structure. Removed function
 *                 declarations FCfontEmRes, FCfontBBox. Added FCfontGlobal.
 * 01-Sep-94  jfd  Added 'strings' field to FCTYPE structure.
 *                 Added 'offsetDescrip' to FONTTYPE structure.
 *                 Added arg to FCfontInfoTT() function.
 *                 Changed char arrays of TTFONTINFOTYPE structure
 *                 to char *'s.
 *                 Changed FCfontInfoTT() to return TTFONTINFOTYPE*.
 *                 Changed arg list of FCfontInfoTT() to TTFONTINFOTYPE*, 
 *                 FONTTYPE* and FCTYPE*.
 * 02-Sep-94  mby  Added declaration of FCpluginNew().
 * 07-Sep-94  SBM  Added sFamilyClass, fsSelection, orThreshold, standardCutin,
 *                 and stanStanCutin in TTFONTINFOTYPE.
 * 09-Sep-94  mby  Added TFGLOBAL.spaceBand, FCTYPE.numCharGroups,
 *                 CHARTYPE.stanIndex.  Changed calling parameters of
 *                 FCtableNew, FCchmapLookup, FCfontGlobal, FCfontInfoTT.
 *                 Moved 'TTFONTINFOTYPE tfDesc' from FCTYPE to FONTTYPE.
 *                 Revised CHMAPTYPE & FONTTYPE for FCO v.6 format.
 * 13-Sep-94  mby  Added TTFONTINFOTYPE.pluginDescriptor. Removed redundant
 *                 data from TFGLOBAL structure. Added CHARTYPE.segmSharing.
 * 14-Sep-94  mby  Added SEGSHAR_EMASK to mask sharing bits from character width.
 * 15-Sep-94  dbk  Added LINTARGS for non-ANSI compliance.
 *                 Moved function prototypes to bottom to resolve compiler
 *                 error.
 * 19-Sep-94  mby  Move static function declarations into fc_da.c
 * 25-Oct-94  mby  In FCTYPE change "strings" and "yClassData" to MEM_HANDLE
 *                 for DISKFILE=1.
 * 27-Oct-94  mby  In TTFONTINFOTYPE char* pointers changed to ushort's. These
 *                 are indices into the String Table.
 *                 Remove prototype for FCnumTables().
 * 27-Oct-94  mby  Put back FCnumVersion().
 * 04-Nov-94  mby  Made changes to CHMAPTYPE to support complex CCs.
 * 09-Nov-94  mby  Remove CHMAPTYPE.chGrIx if ADDCPLXCC is defined.
 * 15-Nov-94  mby  Add "cplxCCEsc", "cplxCCXYoff" arrays to FONTTYPE.
 *                 Changed COMPCHARTYPE.shortForm to COMPCHARTYPE.compCharType.
 *                 Added "longXOff", "longYOff, "longCCFlag" to COMPPIECETYPE.
 * 23-Nov-94  mby  Added prototype for FCgetPostStringsPtr(). Added new
 *                 structure members CHARTYPE.psoffset & FCTYPE.postStrings
 * 01-Dec-94  bjg  Added "FCWstan..." to FCTYPE structure.
 * 01-Dec-94  mby  Added "chmapFormatPS" to CHMAPTYPE structure.
 * 01-Dec-94  mby  Removed ADDCPLXCC; CHMAPTYPE.chGrIx.
 * 07-Dec-94  mby  Added CHMAPTYPE.mapTable1a, CHMAPTYPE.numAccentExcs.
 * 15-Dec-94  mby  Added FONTTYPE.fontIx. Added COMPPIECETYPE.fontData for
 *                 support of External Compound Characters.
 * 11-Jan-95  mby  Added "charGroupOffsH" to FCTYPE structure.
 * 13-Jan-95  mby  Added "numYdimens", "numXdimens" to FCTYPE structure.
 * 23-Jan-95  bg   Changed COMPLXFLAGSMASK to 0xFFF.
 *                 Changed COMPLXFLAGSSHIFT to 12.
 *                 Added defines for SPECIALCASEMASK and EXSPECIALCASEMASK.
 * 30-Jan-95  mby  Changed malloc() memory allocation to handle-based using
 *                 BUFalloc(). In FONTTYPE replaced "excTbl" with "excTbl_CCinfoH".
 *                 In CHMAPTYPE replaced "mapTable0" with "mapTablesH".
 * 30-Jan-95  bg   Added "FCTYPE.dimTypesH".
 * 08-Feb-95  mby  'XdimProjTypeOff' moved to FCTYPE structure from INFLATE.
 *                 Changed malloc() memory allocation to handle-based using
 *                 BUFalloc(). Affects COMPCHARTYPE, CHARTYPE structures.
 * 01-May-95  mby  Added 'maxPoints' & 'coordBufH' to FCTYPE.
 *                 Added CVFTYPE structure to support convergent fonts.
 * 09-May-95  mby  Added "cvHorAdjListH", "cvProhItalOff" to CVFTYPE struct.
 * 24-May-95  mby  Added CVFTYPE.altFontH, CHARTYPE.altEscapement.
 * 24-May-95  mby  Removed FONTTYPE.TFglobal. Removed TFGLOBAL structure.
 *                 Added declaration for FCfontInfoIndex().
 * 01-Jun-95  mby  Added FONTTYPE.shareFontH; added 3rd argument to FCfontNew().
 * 16-Jun-95  mby  For Convergent Fonts added CHARTYPE.altGlyph; changed
 *                 CHARTYPE.segmSharing to byte; changed SEGSHAR_XX constants.
 *                 Removed "int" data types from CVFTYPE.
 * 27-Jun-95  mby  Changed last arg of FCpluginNew() from UB8* to PTREE*.
 *                 Moved PTREE and TREETYPE structure defs to this file.
 * 21-Jul-95  mby  Added TTFONTINFOTYPE.FontBBoxXmin, & FontBBoxXmax.
 *                 Moved VERSION... definitions here from 'fc_if.c'
 * 25-Aug-95  mby  Added new argument to FCcharNew(); defined ALTGLYPH
 * 30-Aug-95  mby  Added "IF_extCC" & "IFextCC_offs" to CHARTYPE to handle
 *                 IF external CCs.
 * 02-Oct-95  mby  Cleanup: removed VERSION_0F definitions.
 * 13-Feb-96  mby  Increase MAX_PTREES to 5, for PS plugin support.
 * 14-Feb-96  bjg  Changed VERSION_NUMBER defn to 0x12.
 * 05-Mar-96  mby  Added CHARTYPE.partEscapement.
 * 07-May-96  mby  Added support for Version 0x13 - algorithmic narrowing:
 *                  CVFTYPE.narrowFactor; changed usage of FONTTYPE.cvfFlag.
 * 18-Jun-96  mby  Added CVF_NW_OB constant.
 * 05-Nov-96  mby  FCfontInfoIndex() declared for FNT_METRICS=1.
 * 16-Jan-97  SBM  Added usWinAscent and usWinDescent fields in TTFONTINFOTYPE.
 * 05-Mar-97  mby  Restored 11/05/96 change.
 * 14-Apr-97  mby  Replaced "LINTARGS" with "LINT_ARGS".
 * 08-Aug-97  mby  Modified function prototypes of FCtable and FCchmap
 *                 routines to return a UW16 error status code.
 * 20-Aug-97  mby  Modified function prototypes of FCnew/close, FCfontNew/Close,
 *                 FCcharNew/Close, FCcompChar..., FCpluginNew, FCfontInfoIndex,
 *                 routines to return a UW16 error status code.
 * 21-Nov-97  mby  Conditionally removed structure members and function
 *                 declarations based on { #if PLUGINS },
 *                 { #if FCO_CONVERGENTFONT }, and { #if FCO_CURVESHARE }
 * 10-Mar-98  slg  Don't use "long" dcls (incorrect if 64-bit platform)
 * 31-Mar-98  slg  Replace !DISKFILE test by FCO_ROM.
 * 12-Jun-98  slg  Move all fn prototypes to shareinc.h
 * 21-Jul-98  tbh  typedef intermedTable for MM2 global dims, VERSION_14
 * 04-Aug-98  tbh  escapementDefs added to FCTYPE for VERSION_14
 * 12-Aug-98  slg  Make VERSION_NUMBER conditional on MM2
 * 13-Aug-98  tbh  added charDataBitPos for defining bit in charData stream 
 *                 at which local dims begin
 * 24-Aug-98  jfd  Changed all references of MM2 to FCO2.
 * 26-Aug-98  tbh  introduced maxAssocs to support proper allocation of skelNodes
 * 01-Oct-98  tbh  rstackH
 * 15-Apr-99  keb  added tt->FontBBoxYmax & tt->FontBBoxYmin to TTFONTINFOTYPE   
 * 28-Jul-99  ks   Changed DEBUG compiler directive to AGFADEBUG. 
 * 17-Aug-99  slg  In TREETYPE structure, change treeIx[] from char to SB8
 *					(fix required for SIGNEDCHAR NO compiler)
 * 31-Jan-00  slg  Integrate disk/rom changes (for jd) - add FC_ACCESS struct
 *				   (which contains both ROM and DISK-style data pointers),
 *				   use FC_ACCESS within FC_TYPE.
 * 08-Jan-00  slg  Add "font_access" field to FONTTYPE struct.
 * 30-Jul-03  jfd  Added FCO_PREALLOCATE support.
 */
 
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

#if FCO_CONVERGENTFONT
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
#endif  /* FCO_CONVERGENTFONT */

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
#if FCO_CURVESHARE
    MEM_HANDLE  shareFontH;  /* handle to new font for curve sharing */
#endif  /* FCO_CURVESHARE */
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
	UW16	font_access;	/* set by user to identify font access by */
								/* DISK (DISK_ACCESS) or ROM (ROM_ACCESS) */
    TTFONTINFOTYPE  tfDesc;  /* Typeface global information */
    SL32       cvfFlag;       /* Convergent Font segment flag
                              *   0: not CVF
                              *   1: standard Convergent Font
                              *   2: narrowed Convergent Font (less data) */
#if FCO_CONVERGENTFONT
    CVFTYPE   cvf;           /* Convergent Font data structure */
#endif
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
#if FCO_CONVERGENTFONT
    SL32         altEscapement; /* escapement of Convergent Font char */
    UB8 altGlyph;    /* Convergent Fonts: 0 or 1 for set width adj, oblique matrix */
#endif  /* FCO_CONVERGENTFONT */
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



#define MAX_PTREES   5     /* max nr plugin trees (prop; TT; PS; FP-1; FP-2) */
typedef struct {
    UB8 wspu[15];
} PTREE;

typedef struct {
    SL32    nTrees;
    SB8    treeIx[MAX_PTREES];
    PTREE  tree[MAX_PTREES];
} TREETYPE;


#endif	/* __FC_DA__ */

