
/* Copyright (C) 2008-2013 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* cgconfig.h */


/* This file should be included in all .c files of the UFST core.          */
/* The setting of defines here determines the functionality options to be  */
/* included in the resulting libraries and executables.                    */
/* These defines are used to conditionally compile code in the core.       */

/* Setting a define to 1 includes that option, 0 excludes it.              */

/* ** When altering settings, make sure to note dependencies specied. **   */


#ifndef __CGCONFIG__
#define __CGCONFIG__


/****************************************************************************

ATTENTION:

Please read the following notice carefully before using this kit in any way. 

You will find evaluation software and fonts provided by Monotype Imaging in
this kit for which your company may not be licensed.

Therefore, you must make sure that you have a valid license to use any of the
software components or fonts. To ensure that you are licensed, or if you have
any questions regarding this notice, please contact your Monotype Imaging 
Business Manager directly. You may also contact us at www.monotypeimaging.com.

Thank you, from Monotype Imaging Inc.

****************************************************************************/

/*--------------------------------*/
/* Global Graphics Customisations */
/*--------------------------------*/

#define GG_CUSTOM         0  /* Switch on customisations to the UFST source */

/*---------------------------*/
/* Memory Management Options */
/*---------------------------*/

/* Non-CG (external) memory management can be provided outside of the UFST  */
/* core and referenced via extmem.c by setting INT_MEM_MGT==0. C system     */
/* calls malloc & free can be the basis for external memory management.     */
#define INT_MEM_MGT       0  /* CG (Internal) memory management scheme      */
#define DEFUND            0  /* requires INT_MEM_MGT==1                     */


/*------------------------------*/
/* Character Generation Options */
/*------------------------------*/

/*  At least one of CACHE, CHAR_SIZE or CHAR_HANDLE must be set to 1        */
#define CACHE             0  /* CG caching of character bitmaps             */
#define CHAR_HANDLE       1  /* Non-caching character generation            */
#define CHAR_SIZE         1  /* char size info and bitmap generation        */

#define CACHE_BY_REF	  0	/* if CACHE=1, and CACHE_BY_REF=0, use original caching */
							/* if CACHE=1, and CACHE_BY_REF=1, use cache-by-reference-count */
						

/*-----------------------------------*/
/* Bitmap and Outline Output Options */
/*-----------------------------------*/
#define CGBITMAP          1  /* generate bitmaps                            */
#define LINEAR            1  /* Linear outlines                     [was 0] */
#define QUADRA            0  /* Quadratic outlines                          */
#define CUBIC             1  /* Cubic outlines                      [was 0] */
#define GRAYSCALING       0  /* graymap output                              */

#define OLDATA            1  /* enable CG outline functions         [was 0] */
                             /* required if LINEAR, QUADRA or CUBIC == 1    */

#define TILE              0  /* generate characters in tiles                */

/*  The banding option will support caching of horizontal bands of character
 *  bitmaps. By a horizontal band, we mean a tile whose width is the
 *  entire width of the character bitmap.
 */
#define BANDING           0

#define SPECIAL_EFFECTS   0  /* Allow generation of special effects, Emboss, Engrave, etc */
                             /* Can NOT be used with TILE or BANDING */
                             
#define DYNAMIC_FONTS     1  /* can use IF fonts, that are not in IF.FNT block [was 0] */
#define NON_Z_WIND        1  /* specify non-zero winding transitions */

#define FIX_CONTOURS      1     /* 1 - enable fixing of badly wound contours */

/* Bitmap size limits                                                     */
/* Maximum bitmap size specified as power of 2 (e.g., 16 == 64K-1 bytes)  */
/* For MSDOS, 16 is the recommended limit for MAX_BM_BITS.                */
/* For UNIX,  22 is the limit due the use of this define with long ints.  */
#define MAX_BM_BITS      22


/*------------------*/
/*  Plugin Options  */
/*------------------*/
#define SLIM_FONTS        1  /* Use HQ4 plugins, support HQ4 (S) libraries  */
#define CGFIXED_PITCH     0  /* Include support for Fixed Pitch plugins     */

#define TT_PLUGINS        0  /* TrueType Plugin Options
                              * 0 no plugins
                              * 2 TT plugins (LaserJet 4) -- TT format
                              */
#define PS_PLUGINS        0  /* PostScript Plugin Options
							  * 0 no plugins
							  * 1 IF plugins 
							  */

/*------------------*/
/* Data Input Types */
/*------------------*/
/* Disk input */
#define IF_DISK           0  /* Intellifont disk input                      */
#define PST1_DISK         0  /* PostScript Type 1 disk input                */
#define CFF_DISK          0  /* PostScript CFF disk input                   */
#define TT_DISK           0  /* TrueType disk input                         */
#define FCO_DISK          0  /* FCO disk input                              */

/* PCLEO formatted input */
#define IF_PCLEOI         1  /* Intellifont PCLEXO input                    */
#define PST1_SFNTI        0  /* PostScript type 1 soft font input           */         
#define TT_PCLEOI         1  /* TrueType PCLEXO input                       */
#define TT_TYPE42I        0  /* TrueType Type 42  input                     */

/* ROM input */
#define IF_ROM            1  /* IF ROM input (Plugins, core fonts in ROM)   */
#define PST1_RAM          0  /* PostScript Type 1 RAM input                 */
#define CFF_ROM           0  /* PostScript CFF ROM input                    */
#define TT_ROM            1  /* TrueType ROM input                          */
#define FCO_ROM           1  /* FCO ROM input                               */
#define TT_ROM_ACT        0  /* AGFA Compressed TrueType ROM input          */

/* You can only enable one of (FCO1, FCO2, FCO3) in a single build of UFST */
#define FCO1              0		/* use MICROTYPE1 Data Format? (Version 0x13 and below)	*/
#define FCO2              0		/* use MICROTYPE2 Data Format? */
#define FCO3              1		/* use MICROTYPE3 Data Format? (new in UFST 5.0) */

#define T1MMASTER         0  /* type 1 multimaster                          */

#define DIMM_DISK         0  /* font in rom uses disk i/o to access data    */
                             /* currently, this only works for TT_DISK mode */

#define LINKED_FONT		0		/* 1 - enable linked fonts */

#define MTI_PCLEO_DEMO 1	/* set to 1 to enable use of MTI's sample code for PCLEO processing */
							/* set to 0 if you're implementing your own PCLEO processing */
#define MTI_TYPE42_DEMO 0   /* set to 1 to enable use of MTI's sample code for Type 42 processing */
							/* set to 0 if you're implementing your own Type 42 processing */
							
#define DEMO_SHIPPING 1 /* to build demo.c in DEMO (shipping) mode */
#define DEMO_EMBEDDED 2 /* to build demo.c in DEMOEMBEDDED (internal) mode */
#define DEMO_EFFECTS 3 /* to build demo.c in INHOUSEDEMOSP (internal) mode */
#define DEMO_USERDEF 4 /* to build demo.c using your own set of initialization values */
#define DEMO_BITMAPCOMPARE 5 /* to build demo.c for use with Python bitmap compare script */

#define WHICH_DEMO DEMO_SHIPPING							

/*------------------*/
/* TrueType Options */
/*------------------*/



#define EMBEDDED_BITMAPS  1  /* TT Embedded bitmap input                    */

#define USE_RAW_GLYPHS    0  /* TT raw-glyph-index support (no CMAP table)  */

#define STIK              0     /* 1 - enable stroke font processing */
#define CCC               0     /* 1 - enable CCC = compressed stroke/TT font processing   */
#define FS_EDGE_HINTS     0     /* 1 - enable EDGE font graymap hinting */
#define FS_EDGE_RENDER    0     /* 1 - enable EDGE font graymap rendering */

#define ARABIC_AUTO_HINT  0		/* 1 - enable autohinting of Arabic stroke font characters */

#define TT_SCREENRES      0    /* enable special processing for TT at very small screen resolutions?  */


/*-------------------*/
/* MicroType Options */
/*-------------------*/

#define HP_4000           0  /* allow bucket searching and data scaling
                              * to emulate HP4000 (at 2048 design units).
                              * 0 = NO HP4000 emulation
                              * 1 = enable HP4000 emulation
                              *
                              * For use with FCO_DISK or FCO_ROM.
                              * Also, DU_ESCAPEMENT should be set to 1.
                              */

#define FCOACCESS         1  /* allow independent access to FCO segment data
                              * 0 no access
                              * 1 access via pathname & fco_index
                              */

#define USBOUNDBOX     0		/* enable MicroType unscaled bounding box option? */

#define MAX_FCOS 		  3		/* what is the max # of FCOs that can be loaded at once? */


/*---------------------*/
/* Intellifont Options */
/*---------------------*/

#define SEGACCESS         0  /* allow independent access to IF segment data
                              * 0 no access
                              * 1 access via font id
                              */
                              
/*---------------------*/
/* Pseudo Bold Options */
/*---------------------*/

#define BOLD              0  /* allow pseudo emboldening                    */
#define BOLD_P6           1  /* emboldening for PCL 6 emulation             */
#define BOLD_HORIZONTAL   0
#define BOLD_VERTICAL     0
#define BOLD_CJK 		  0


/*---------------------------------*/
/* Scaling Parameter Input Options */
/*---------------------------------*/
/* Set SCALE_MATRIX to 0 for FONTCONTEXT compatible with Version 1.4
 * and earlier. Set to sum of any of the following 4
 * defines for the FONTCONTEXT types you wish to support.
 * Do Not Change the MATRIX types, only change SCALE_MATRIX.
 */
#define TYPO_SCALE_MATRIX    0x0001   /*  typographer                     */
#define MAT0_SCALE_MATRIX    0x0002   /*  matrix from design to output    */
#define MAT1_SCALE_MATRIX    0x0004   /*  matrix world->out and em box    */
#define MAT2_SCALE_MATRIX    0x0008   /*  matrix world->out and pt/size   */

#define SCALE_MATRIX      (TYPO_SCALE_MATRIX | MAT0_SCALE_MATRIX)

#define VLCOUTPUT         1    /* Very Large Character Output */


/*----------------------------------*/
/* Raster Line Organization Options */
/*----------------------------------*/
/*  Character bitmaps produced by UFST are structured as a sequence of
 *  horizontal raster lines from top to bottom of the bitmap. Each raster
 *  line is structured as a sequence of bytes. Applications would like to
 *  retrieve 8, 16, or 32 bit portions of a raster line and 
 *  would like the bit ordering of the bits within these larger (than byte)
 *  portions to be defined by the application. RASTER_ORG controls the
 *  size of the raster "chunk". LEFT_TO_RIGHT_BIT_ORDER contrls the
 *  bit ordering within the "chunk".
 *
 *  Set RASTER_ORG to any of the 3 defines for the "chunk" size you wish to
 *  support. Do Not Change the CHUNK types, only change RASTER_ORG.
 *
 *  Set LEFT_TO_RIGHT_BIT_ORDER to 1 for left-to-right bit ordering, 0
 *  for right-to-left bit ordering.
 */

#define EIGHT_BIT_CHUNK     0x0001   /* 8-bit chunk (default) */
#define SIXTEEN_BIT_CHUNK   0x0002   /* 16-bit chunk          */
#define THIRTYTWO_BIT_CHUNK 0x0004   /* 32-bit chunk          */

#define RASTER_ORG	EIGHT_BIT_CHUNK
#define LEFT_TO_RIGHT_BIT_ORDER  1   /* 1 = left-to-right bit ordering  */
                                     /* within chunk, 0 = right-to-left */

/*------------------------*/
/* Font Metrics Options   */
/*------------------------*/
/*  If set to 1, this option enables the function CGIFfont_metrics().
 */
#define FNT_METRICS       1

/*  If set to 1, this option supports the CGIFbound_box() function that
 *  returns an upper bound estimate of the font bounding box in output pixels.
 */
#define FONTBBOX          0

#define WIDTHS            1  /* allow independent access to width data      */

#define XPS_SIDEWAYS 0		/* return new vertical positioning info in IFBITMAP and IFOUTLINE */

/* If DIRECT_TT_TABLE_ACCESS is set to 1, TrueType tables in ROM/RAM can be accessed 
directly (with memory pointers),by using the CGIFtt_query_direct() call.
 
Note that, even if DIRECT_TT_TABLE_ACCESS is not set, copies of TrueType tables 
can still be obtained by using the CGIFtt_query() call. */

#define DIRECT_TT_TABLE_ACCESS	0		

/* Font Metrics Precision:
 *  If DU_ESCAPEMENT is set to 1, the escapement field of the character
 *  will be in the design units specified in "du_emx" and "du_emy" of the
 *  IFBITMAP/IFOUTLINE data structures. Thus, "escapement/du_emx" is the
 *  fraction of an em to escape. If DU_ESCAPEMENT is set to 0, then the
 *  escapement will be returned in 8782 units per em as is now.
 *
 */
#define DU_ESCAPEMENT     1  /* Escapement design units:                    */
                             /*  0 == use 8782/em, 1 == use native units    */

                                                                            
/*------------------------*/
/*  Font Encoding options */
/*------------------------*/

#define SJIS_ENCODING     0     /* enable Kanji Shift-JIS encoded font support            */
#define JIS_ENCODING      0     /* enable Kanji JIS encoded font support                  */
#define EUC_ENCODING      0     /* enable Kanji EUC encoded font support                  */

#define BIG5_ENCODING     0     /* enable BIG5 encoded font support - Chinese (Hong Kong) */
#define TCA_ENCODING      0     /* enable TCA encoded font support - Taiwanese            */
#define GB_ENCODING       0     /* enable GB encoded font support - Chinese (Mainland)    */
#define K_ENCODING        0     /* enable K encoded font support - Korean (KSC, Wansung)  */
#define WANSUNG_ENCODING  0     /* enable WANSUNG encoded font support - Korean (WANSUNG) */
#define JOHAB_ENCODING    0     /* enable JOHAB encoded font support - Korean (JOHAB)     */

#define UNICODE_ENCODING  1     /* enable Asian UNICODE encoded font support              */

#define SYMBOL_ENCODING   0     /* enable Asian SYMBOL encoded font support               */

#define UCS4_ENCODING     0     /* enable Asian UCS4 encoded font support                 */


/*---------------------*/
/* Transcoding Options */
/*---------------------*/

/* This define indicates which Text Data Stream Encodings can be transcoded    */
/* to UNICODE values when the Text encoding DOES NOT match the Font Encoding   */
/* If any Asian encoding AND UNICODE_ENCODING are both turned on, then reverse */
/* transcoding (UNICODE-to-xxx) is also enabled */

#define UNICODE_MAPPING   0     /* Bits indicating appropriate transcodings
                                 * Set UNICODE_MAPPING to the numeric sum of
                                 * the options you want turned on.
                                 * (decimal values)
                                 *
                                 * 0 - No transcoding
                                 * 1 - JIS To Unicode transcoding
                                 * 2 - Big5 To Unicode transcoding
                                 * 4 - GB To Unicode transcoding
                                 * 8 - KSC To Unicode transcoding
								 * 16 - Johab to Unicode transcoding 
                                 */

/*-------------------*/
/* Mapping Options   */
/*-------------------*/

/* The USE_UFST_MAPPING value is used for PostScript Type 1 processing, 
	but NOT for CFF processing. */
#define USE_UFST_MAPPING  1     /* 1-PCL sym sets, 0-encoding array, 2-both */

#define NO_SYMSET_MAPPING 0     /* 0 - use UFST symbol sets for mapping (recommended) */


/*--------------------------*/
/* Multithreading Options   */
/*--------------------------*/

/* The default for UFST_REENTRANT is 0 - this will build UFST in the usual way,
operating on a single global copy of the IF_STATE structure. In order to build
with UFST_REENTRANT on, your application needs to create its own instances of
the IF_STATE structure, and pass a pointer to an instance when calling UFST.
*/ 

#define UFST_REENTRANT	0

/* The default for UFST_MULTITHREAD is 0 - this will build UFST in the usual way,
operating on a single global copy of thte IF_STATE structure. In order to build
with UFST_MULTITHREAD on, your application needs to create its own instances of
the IF_STATE structure, and pass a pointer to an instance when calling UFST.
To get a multi-thread safe version, define the following -- requires UFST_REENTRANT */

#define UFST_MULTITHREAD	0


/*---------------------*/
/* Debugging Options   */
/*---------------------*/

/* for LOTS of debugging information, define AGFADEBUG here, 
	and set if_state.trace_sw by using the UFST_debug_on() API call. */
#undef AGFADEBUG

/* Note that some of the debug options will only print output if the standard
	debug is enabled (i.e., if AGFADEBUG is defined, and if_state.trace_sw is set
	by using the UFST_debug_on() API call) */
	
/* define to enable hint debugging in PostScript code */
#undef HINT_DEBUG

#define SHOWINPUT    	0		/* set to 1 to print out Type1 input in t1iscan.c */
#define NZ_DUMP			0   	/* set to 1 to dump lists in nzwind.c */

#define CHECK_MEM_EXT	0		/* set to 1 for external-memory overwrite check */
#define MEM_TRACE   	0      	/* set to 1 to trace MEMalloc() & MEMfree() */
#define PRINT_BUCKETS	0		/* set to 1 to print out all buckets */
#define DISPLAY_STATS   0  		/* set to 1 to display linked-list ptrs in dll.c */
								/* in insert_at_head_of_list(), remove_from_list() */

#define ASIAN_HASH_TRACE  0  	/* set to 1 to trace the Asian Hash Table */

#undef MMDECODE_DEBUG			/* define to enable MT debug in fc_intf2.c, fc_da.c */
								/* MMDECODE_DEBUG uses "mmdecode_trace_sw" global vbl */

#define GRAYSTATS_1 0   		/* set to 1 to enable grayscale memory stats */

#undef	CHECK_MEMORY_ACT		/* define for ACT memory check in memory.c */

#define THREAD_DUMP    0        /* debug option for multithreading */

#define CHECK_TT_BADHINT 0		/* debug option to detect bad-hint error recovery in TT core */

#define	PS_ERRORCHECK	0	/* if set to 1, enable additional errorchecking of PostScript 
									input (initially, Type1 stackchecking) */

/**************************************************************************/
/**--**--**              DO NOT CHANGE below this line.          **--**--**/
/**************************************************************************/

#undef OLD_PST1_SFNTI        /* #define to use legacy mode for Post Script type 1 soft font api (not recommended) */

#define IFBITSPLIT	0		/* split IFBITMAP into 2 structures? (header/bitmap) */

/* define to force align to 32-bit boundaries for global tables in TTPCLEOs */
#define ALIGN_TTPCLEO_TABLES	0

#define WIDTH_NOSYMMAP	   0	/* currently-unused variant of CGIFwidth function */
								/* (designed to work with old NO_SYMSET_MAPPING option) */

#define BOLD_FCO          0     /* emboldening for MicroType */

#define FCO_STANDALONE	0	/* set this to 1 ONLY if building "fcodump2" */

/* set to 0 to build demogr.c in DEMOGR (shipping) mode */
/* set to 1 to build demogr.c in UFST_GRID_PRF (internal) mode */
#define WHICH_DEMOGR 0

#define FS_EDGE_DUMP_OUTLINE 0  /* 1 - enable dump of EDGE font outlines */									
#define ITYPE_EDGE_COMPARE 0	/* set to 1 to add additional test output & modify test parameters */
								/* (useful when implementing or updating EDGE in UFST) */
#define ADJ_MAT1_SCALE_FACT 0  /* set to 1 to adjust the mat1 scale factor by a fraction of point so that it’s output matches others */
#define MATCH_SCALE_MATRIX_OUTPUT 0	/* set to 1 to produce matching output regardles of scale matrix input */

/*-------------------------------------------------------------------------
USE_JUMP_TABLES was implemented for systems using overlay managers which
can not properly handle indirect calls through pointers. The default is
to use the jump tables by setting this value = 1. Setting the value = 0
will cause all calls to be made directly. The code size will be slightly
larger and the performance slightly faster.
---------------------------------------------------------------------------*/
#define USE_JUMP_TABLES   1

/* shipping code should always set QE_ITYPE_BOLD_TEST to 0 */
/* to force itype compatibility for internal tests, set this to 1 */
#define QE_BOLD_TEST	0

/*-------------------------------------------------------------------*/
/*       Build-DLL options											 */
/*-------------------------------------------------------------------*/
/* defines for possible values of UFST_LINK_MODE */
#define UFST_STATIC_LINK	0	/* use this to build static-linked UFST libraries/apps */
#define UFST_BUILD_DLL		1	/* use this to build UFST libraries as DLL */

/* "UFST_LINK_MODE" should be set to "UFST_STATIC_LINK", unless you are building
a Windows UFST application that uses UFST as a DLL (for example, in a multi-
threaded application). In that case, you must build the UFST DLL with this flag
set to "UFST_BUILD_DLL". */

#define UFST_LINK_MODE	UFST_STATIC_LINK

/*-----------------------------*/
/* Dependent and Fixed Defines */
/*-----------------------------*/

/* Max size of bitmap in bytes. Relies on previously defined MAX_BM_BITS */
#define MAX_BM          (UL32)((1 << (UL32)MAX_BM_BITS) - 1)

#define IF_RDR          (IF_DISK || IF_PCLEOI || IF_ROM)
#define PST1_RDR        (PST1_DISK || PST1_RAM || PST1_SFNTI || CFF_DISK || CFF_ROM)
#define CFF_RDR         (CFF_DISK || CFF_ROM)
#define TT_RDR          (TT_DISK || TT_PCLEOI || TT_ROM || TT_ROM_ACT || TT_TYPE42I)
#define FCO_RDR         (FCO_DISK || FCO_ROM)

#define PS_RESIDENT		(PST1_DISK || PST1_RAM || CFF_DISK || CFF_ROM)	

#if CFF_RDR
#undef T1MMASTER
#define T1MMASTER 1
#endif 

/* 1-PCL sym sets, 0-encoding array, 2-both */
#define USE_PS_ARRAY	((USE_UFST_MAPPING == 0) || (USE_UFST_MAPPING == 2)) 
#define USE_PS_SYMSET	((USE_UFST_MAPPING == 1) || (USE_UFST_MAPPING == 2))	    


/*
   Curve data compression is a FCO post-process that works for:
      Microtype  I's version 0x13 FCOs ( FCO2 == 0 ), and
      Microtype II's version 0x14 FCOs ( FCO2 == 1 ).
   UFST code can process either version of FCOs with or without
   curve data compression. Although this configuration was used
   during development, it is not suitable for release. The release
   version for Microtype I uses uncompressed FCOs, and the release
   version for Microtype II uses compressed FCOs.
*/

#define COMPRESSED            (0x01)
#define UNCOMPRESSED          (0x10)

#if FCO2 || FCO3
#define FCO_CURVE_DATA  ( COMPRESSED )
#else
#define FCO_CURVE_DATA  ( UNCOMPRESSED )
#endif


#define DISK_FONTS      (IF_DISK || PST1_DISK || CFF_DISK || TT_DISK || FCO_DISK)
#define PCLEO_RDR       (IF_PCLEOI || PST1_SFNTI || TT_PCLEOI)
#define ROM             (IF_ROM || PST1_RAM || CFF_ROM || TT_ROM || FCO_ROM || TT_ROM_ACT)

#define PLUGINS         (IF_RDR || TT_PLUGINS || PS_PLUGINS || FCO_RDR)
#define TT_NOPLUG       (TT_PLUGINS == 0)
#define TT_TTPLUG       (TT_PLUGINS == 2)
#define PS_NOPLUG       (PS_PLUGINS == 0)
#define PS_IFPLUG       (PS_PLUGINS == 1)

/* OUTLINE will be set if any of the outline options have been specified. */
#define OUTLINE         (LINEAR || QUADRA || CUBIC)

#define SMEAR_BOLD      (BOLD_P6  || BOLD_HORIZONTAL || BOLD_VERTICAL)

#define ASIAN_ENCODING  \
   (SJIS_ENCODING || JIS_ENCODING || EUC_ENCODING || GB_ENCODING\
    || UNICODE_ENCODING || BIG5_ENCODING || TCA_ENCODING\
    || K_ENCODING || WANSUNG_ENCODING || JOHAB_ENCODING || SYMBOL_ENCODING\
    || UCS4_ENCODING || USE_RAW_GLYPHS)

#if !defined(ASIAN_ENCODING)
#define ASIAN_ENCODING 0
#endif

#define USE_ASIAN_CACHE (ASIAN_ENCODING || NO_SYMSET_MAPPING)

/*
 * Unicode mapping required whenever a variety of encodings
 * are specified.  This turns on the reverse direction transcoding
 * capabilities so that UFST can do UNICODE-to-'XXX', as well as
 * 'XXX'-to-UNICODE transcoding.
 */
#define UNICODE_IN      \
   ((SJIS_ENCODING || JIS_ENCODING  || EUC_ENCODING || K_ENCODING \
                   || BIG5_ENCODING || GB_ENCODING || JOHAB_ENCODING) && UNICODE_ENCODING) 

/*
 * The following are EDGE-specific settings. Do not modify.
 */
#if FS_EDGE_RENDER || FS_EDGE_HINTS

#define FS_EDGE_TECH   /* defines code that is common to both rendering and hinting */

#endif	/* FS_EDGE_RENDER || FS_EDGE_HINTS */
                
/*-------------------------------------------------------------------*/
/*               Test for invalid switch settings                    */
/*-------------------------------------------------------------------*/

#if (DEFUND && !INT_MEM_MGT)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   INT_MEM_MGT must be 1 if DEFUND is 1!")
#pragma message("   ****************************************************\n")
#error (DEFUND && !INT_MEM_MGT)
#endif

#if (DEFUND && CACHE && CACHE_BY_REF)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   CACHE_BY_REF option is incompatible with DEFUND!")
#pragma message("   ****************************************************\n")
#error (DEFUND && CACHE && CACHE_BY_REF)
#endif

#if (!CACHE && !CHAR_SIZE && !CHAR_HANDLE)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   At least one of CACHE, CHAR_SIZE or CHAR_HANDLE must be 1!")
#pragma message("   ****************************************************\n")
#error (!CACHE && !CHAR_SIZE && !CHAR_HANDLE)
#endif

#if (!CGBITMAP && !OUTLINE && !GRAYSCALING)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   At least one of CGBITMAP, OUTLINE or GRAYSCALING must be 1!")
#pragma message("   ****************************************************\n")
#error (!CGBITMAP && !OUTLINE && !GRAYSCALING)
#endif

#if (BANDING && (!TILE || !CACHE))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   TILE and CACHE must be 1 if BANDING is 1!")
#pragma message("   ****************************************************\n")
#error (BANDING && (!TILE || !CACHE))
#endif

#if (STIK && !(TT_DISK || TT_ROM || TT_ROM_ACT))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use STIK without TT_DISK, TT_ROM or TT_ROM_ACT")
#pragma message("   ****************************************************\n")
#error (STIK && !(TT_DISK || TT_ROM || TT_ROM_ACT))
#endif

#if (LINKED_FONT && !(TT_RDR && UNICODE_ENCODING))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use LINKED FONT without TT_RDR and UNICODE_ENCODING turned on")
#pragma message("   ****************************************************\n")
#error LINKED_FONT && !(TT_RDR && UNICODE_ENCODING)
#endif

#if (FIX_CONTOURS && !CGBITMAP)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use FIX_CONTOURS without CGBITMAP")
#pragma message("   ****************************************************\n")
#error (FIX_CONTOURS && !CGBITMAP)
#endif


#if (BOLD && !NON_Z_WIND)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use BOLD without NON_Z_WIND turned on")
#pragma message("   ****************************************************\n")
#error (BOLD && !NON_Z_WIND)
#endif

#if (GRAYSCALING && !NON_Z_WIND)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use GRAYSCALING without NON_Z_WIND turned on")
#pragma message("   ****************************************************\n")
#error (GRAYSCALING && !NON_Z_WIND)
#endif

#if (IF_ROM && IF_DISK)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use IF_ROM and IF_DISK options together\n")
#pragma message("   ****************************************************\n")
#error (IF_ROM && IF_DISK)
#endif

#if (FCO_RDR && ((FCO1 && FCO2) || (FCO1 && FCO3) || (FCO2 && FCO3) || (!FCO1 && !FCO2 && !FCO3)))
#pragma message("\n   *****************************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   When using MicroType, must enable exactly one of FCO1, FCO2, FCO3\n")
#pragma message("   *****************************************************************\n")
#error (FCO_RDR && ((FCO1 && FCO2) || (FCO1 && FCO3) || (FCO2 && FCO3) || (!FCO1 && !FCO2 && !FCO3)))
#endif

#if (UFST_MULTITHREAD && !UFST_REENTRANT)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use UFST_MULTITHREAD without UFST_REENTRANT")
#pragma message("   ****************************************************\n")
#error (UFST_MULTITHREAD && !UFST_REENTRANT)
#endif

#if ((FS_EDGE_HINTS && !FS_EDGE_RENDER) || (!FS_EDGE_HINTS && FS_EDGE_RENDER))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   FS_EDGE_HINTS and FS_EDGE_RENDER must be both ON or both OFF")
#pragma message("   ****************************************************\n")
#error ((FS_EDGE_HINTS && !FS_EDGE_RENDER) || (!FS_EDGE_HINTS && FS_EDGE_RENDER))
#endif

#if (FS_EDGE_RENDER && !STIK)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   STIK must be on if FS_EDGE_RENDER is on")
#pragma message("   ****************************************************\n")
#error (FS_EDGE_RENDER && !STIK)
#endif

#if (FS_EDGE_RENDER && !GRAYSCALING)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   GRAYSCALING must be on if FS_EDGE_RENDER is on")
#pragma message("   ****************************************************\n")
#error (FS_EDGE_RENDER && !GRAYSCALING)
#endif

#if (RASTER_ORG != EIGHT_BIT_CHUNK) && (RASTER_ORG != SIXTEEN_BIT_CHUNK) && (RASTER_ORG != THIRTYTWO_BIT_CHUNK)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   RASTER_ORG must be set to EIGHT_BIT_CHUNK, SIXTEEN_BIT_CHUNK, or THIRTYTWO_BIT_CHUNK")
#pragma message("   ****************************************************\n")
#error (RASTER_ORG != EIGHT_BIT_CHUNK) && (RASTER_ORG != SIXTEEN_BIT_CHUNK) && (RASTER_ORG != THIRTYTWO_BIT_CHUNK)
#endif

#if (MATCH_SCALE_MATRIX_OUTPUT && !(SCALE_MATRIX && ADJ_MAT1_SCALE_FACT))
#pragma message("\n   **********************************************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   SCALE_MATRIX and ADJ_MAT1_SCALE_FACT must be on if MATCH_SCALE_MATRIX_OUTPUT is on")
#pragma message("   **********************************************************************************\n")
#error (FS_EDGE_RENDER && !STIK)
#endif
#endif	/* __CGCONFIG__ */

