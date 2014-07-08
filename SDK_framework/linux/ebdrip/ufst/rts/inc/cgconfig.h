/* $HopeName: GGEufst5!rts:inc:cgconfig.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2005 Monotype Imaging Inc. All rights reserved.
 */

/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:inc:cgconfig.h,v 1.6.4.1.1.1 2013/12/19 11:24:04 rogerb Exp $ */
/* cgconfig.h */

/* This file should be included in all .c files of the UFST core.          */
/* The setting of defines here determines the functionality options to be  */
/* included in the resulting libraries and executables.                    */
/* These defines are used to conditionally compile code in the core.       */

/* Setting a define to 1 includes that option, 0 excludes it.              */

/* ** When altering settings, make sure to note dependencies specied. **   */

/* Change History:
 *    1-Jan-92  awr Added #define TT_RDR
 *    9-Feb-92  awr Added #define IF_RDR
 *   26-Feb-92  jfd Added comment to explain meaning of OLDATA switch.
 *   18-Mar-92  rs  Add #define PST1_PCLEOI & TT_PCLEOI for UFST pcleo's.
 *                  Add '#error' in addition to '#pragma message'
 *   03-Apr-92  rs  Portability cleanup (see port.h).
 *   06-Apr-92  rs  Change 'FIXED_PITCH' to 'CGFIXED_PITCH' (portability).
 *   06-May-92  jfd Disabled "#error" statements due to problems on SUN
 *   08-May-92  jfd Changed definition for EXTERN_FONT from
 *                  "(PCLEO_RDR || PST1I || TT_RDR)" to
 *                  "(PCLEO_RDR || PST1_PCLEOI || TT_PCLEOI)".
 *   12 May 92  ss  Added NON_IF_FONT define to signify that one or more
 *                  PS/TT options are enabled.
 *   22-May-92  rs  Add #define USE_UFST_MAPPING for all symbol sets w/PS
 *    5 Jun 92  ss  Cleanup and major reorganization of file.
 *   14-Jun-92  awr Added #define INTR_SIZE
 *    3-Jul-92  awr Changed comments and pragma for MULTI_CALLER
 *    8-Jul-92  jfd Changed comment associated with WIDTHS to indicate
 *                  widths for any type of font (IF, PS or TT).
 *   10-Jul-92  jfd Moved #define for "USE_UFST_MAPPING" to fixed defines
 *                  section because it should not be changed.
 *                  Changed MEM_ALIGN from 1 to 3 for SUN/SPARC and added
 *                  pragma warning for DOS users.
 *   20-Jul-92  rs  Create new IF, PS & TT defines for disk, PCLEO & ROM
 *                  input.
 *   30-Jul-92  jfd Removed olsolete pragmas.
 *   21 Aug 92  ss  Added comments about DISK & ROM being mutually exclusive.
 *   27-Sep-92  awr Added DISK_FONTS, a derived define.
 *   10-Oct-92  rs  Implement USE_JUMP_TABLES feature for overlays.
 *   20-Oct-92  jfd Changed "FF_NOPLUG" to "TT_NOPLUG".
 *   22-Jan-93  jfd Added #define for DU_ESCAPEMENT.
 *   01-Feb-93  jfd Added #define for BANDING.
 *   04-Feb-93  mby Added #defines for FONTBBOX & ALIEN_BITS
 *   27-Mar-93  rs  Add #defines for Kanji support - , SJIS_ENCODIING,
 *                  JIS_ENCODING, EUC_ENCODING & KANJI_ENCODING.
 *   07-Apr-93  jfd Added #defines for RASTER_ORG and LEFT_TO_RIGHT_BIT_ORDER
 *                  for raster line organization support.
 *   07-Apr-93  mby Added #define for DYNAMIC_FONTS.
 *   04-May-93  rs  Add define for 'DRAS_QUAD' - direct rasterization of
 *                  quadratic curves.
 *   06-May-93  jfd Fixed comment regarding IF_DISK.
 *                  Added pragma stating that BANDING requires CACHE.
 *   29-May-93  rs  Add conditional 'PST1_SFNTI' for generic type 1
 *                  downloaded soft font support.
 *   06-Jun-93  rs  Add type 1 MultiMaster support 'T1MMASTER'.
 *   14-Jun-93  rs  Change comment for 'USE_UFST_MAPPING'.
 *   01-Jul-93 maib Added new define NON_Z_WIND to specify non-zero
 *                  winding methods for determining transitions.
 *                  Also, disabled error message "#error Can not enable 
 *                  multiple forms of type 1 soft fonts" due to problems
 *                  on SPARC
 *   06-Jul-93 mby  Added #defines for CONVERGENT_FONTS and MULTIFORMAT.
 *   10-Jul-93 mby  Added #define TT_PLUGINS 3 option for LJ4 compatible, IF format.
 *   23-Jul-93 rs   Add 'T1REMASTER' for type 1 multimaster support.
 *   02-Aug-93 maib Added SEM_ACCESS, to configure semaphore multi-tasking
 *   12-Aug-93 rs   Add 'PST1_SFNTI' to define for 'PST1_RDR'.
 *   31-Aug-93 awr  Set NON_Z_WIND to 1 if BOLD == 1.
 *   02-Sep-93 jfd  Moved the following #defines above the dotted line:
 *                  USE_JUMP_TABLES, DRAS_QUAD, SJIS_ENCODING, JIS_ENCODING,
 *                  EUC_ENCODING, and USE_UFST_MAPPING.
 *   13-Sep-93 mby  Added EXTERN_ENTITY1
 *   17-Sep-93 mby  Changed to TT_ROM1.
 *   09-Nov-93 gy/my  Added UNICODE_ENCODING definition. Expanded definition
 *                  of KANJI_ENCODING to include UNICODE_ENCODING.
 *                  Moved JIS, SJIS, EUC, & UNICODE_ENCODING below the line.
 *   23-Nov-93 rs   Add define for "BIG5" & "TCA". Add these to KANJI_ENCODING.
 *   01-Dec-93 jfd  Changed KANJI_ENCODING to ASIAN_ENCODING.
 *   02-Dec-93 mby  Fixed syntax error in #define ASIAN_ENCODING. "\" was
 *                  not the last character in the line.
 *   07-Dec-93 maib Added UNICODE_IN and UNICODE_OUT, moved asian literals
 *                  above the do-not-change line
 *   08-Dec-93 maib Changed the way in which Unicode mappings are determined
 *   17-Dec-93 jfd  Changed the comment associated with MAX_BM_BITS setting
 *                  for UNIX (max should be 22 not 32).
 *   05-Jan-94 rs   Add #define for "GB_ENCODING".
 *   28-Feb-94 mby  Added pragma to constrain CONVERGENT_FONTS to value of
 *                  0, 3 or 4.
 *   01-Mar-94 mby  CONVERGENT_FONTS can be 0, 1, or 3.
 *   20-Apr-94 jfd  Changed comment associated with SEGACCESS switch.
 *   22-Apr-94 jc   Added pragmas for MULTIFORMAT/CONVERGENT_FONTS.
 *   11-Jul-94 jfd  Moved #define for K_ENCODING here from TTKAN.C .
 *                  Expanded #define for ASIAN_ENCODING.
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * FCO Changes from 1.37.1.2:
 *   11-Mar-94 mby  Added FCO_DISK, FCO_ROM, & FCO_RDR configurations.
 *   26-May-94 mby  Added FCO_TT configuration to the data output types to
 *                  generate a TrueType font from an FCO.
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 *
 *   14-Sep-94 mby  Add FCO_RDR to definition of PLUGINS.
 *   29-Nov-94 jfd  Changed the defines TT_NOPLUG, TT_IFPLUG, TT_TTPLUG,
 *                  TT_TTPLUG_IF, TT_TTPLUG_ANY, PS_NOPLUG and PS_IFPLUG
 *                  based on !FCO_RDR.
 *   02-Dec-94 yan  Ksc.
 *   09-Jan-95 rob  Implement support for > 64KB memory blocks in DOS.
 *                  Add '#define HUGE_PTR_SUPPORT'.
 *   25-Jan-95 jfd  Before redefining NON_Z_WIND (if BOLD), undefine
 *                  NON_Z_WIND to resolve WATCOM compiler error.
 *   10-Feb-95 jfd  If !ASIAN_ENCODING, #define ASIAN_ENCODING as 0
 *                  to resolve compiler error on some platforms.
 *   31-Mar-95 jwd  Added support for FCO Access function, CGIFfco_Access().
 *                  This function is conditionally compilable based on the
 *                  switch FCOACCESS.
 *   18-Apr-95 jfd  Added #define for NO_SYMSET_MAPPING.
 *                  Added pragmas for NO_SYMSET_MAPPING.
 *   20-Jul-95 mby  Removed #define and #pragmas for NO_SYMSET_MAPPING.
 *   24-Jul-95 mby  Defined NO_SYMSET_MAPPING 0 below the line.
 *   07-Sep-95 jfd  Changed #defines for TT_NOPLUG, TT_IFPLUG, TT_TTPLUG,
 *                  TT_TTPLUG_IF, TT_TTPLUG_ANY, PS_NOPLUG and PS_IFPLUG
 *                  by making them independent of FCO_RDR.
 *   22-Sep-95 jfd  Moved DRAS_QUAD define below the DO NOT CHANGE line.
 *   12-Jan-96 jfd  Turn on NON_Z_WIND if GRAYSCALING is on.
 *   24-Jan-96 awr  Added COMPRESSED_CACHE
 *   12-Feb-96 mby  Added PS_PLUGINS values of 2 and 3; PS_MTPLUG, PS_PSPLUG
 *   20-Aug-96 mby  Replaced PSEUDO_BOLD_FCO (below the line) with BOLD_FCO
 *                  (above the line). This enables emboldening for MicroType.
 *   12-Sep-96 dbk  Added GB_ENCODING support to ASIAN_ENCODING define.
 *   12-Sep-96 dbk  Modified defaults to work with DEMO.C as it is delivered
 *                  to OEM's.
 *   23-Oct-96 bjg  Added emboldening (BOLD_P6) for PCL 6 emulation.
 *   05-Nov-96 mby  Added FNT_METRICS to enable CGIFfont_metrics().
 *   08-Jan-97 dlk  Removed KERNing option as part of project to trim ufst.
 *   09-Jan-97 dlk  Added BIG5_ENCODING and GB_ENCODING to define for
 *                  UNICODE_IN.
 *   10-Jan-97 dlk  Removed SCREEN_FONTS option, and ELASTIC_X, and ELASTIC_Y
 *                  as part of project to trim ufst.
 *   13-Jan-97 dlk  Removed CONVERGENT_FONTS option as part of project to
 *                  trim ufst.
 *   14-Jan-97 dlk  Removed MULTIFORMAT option and associated PRAGMAs as part
 *                  of project to trim ufst.
 *   14-Jan-97 dlk  Removed DRAS_QUAD option as part of project to trim ufst.
 *   15-Jan-97 dlk  Removed PST1_PCLEOI option as part of project to trim ufst.
 *   19-Feb-97 dlk  Added defines for 3 CODE PAGE Asian ENCODINGs to for future
 *                  use.  These are 'below' the DO NOT CHANGE line for now.
 *                  Also added comment lines to help make 'mapping' versus
 *                  'transcoding' clearer for Asian support.
 *   29-May-97 sbm  Add TT_ROM_ACT functionality.
 *   04-Jun-97 dlk  Added defines for WANSUNG_ENCODING and JOHAB_ENCODING.
 *                  These are alternate Korean encodings.  No mapping tables
 *                  to/from UNICODE are available at this time.  Also updated
 *                  ASIAN_ENCODING define to reflect these additional cases.
 *   16-Jun-97 awr  Changed configuration error checking to allow
 *                  GRAYSCALING only (no CGBITMAP or OUTLINE)
 *   15-Jul-97 awr  Pseudo bold via outlines.
 *   30-Jul-97 keb  Added define for SMEAR_BOLD.
 *   31-Jul-97 keb  Removed BJG_BOLD_P6.
 *   31-Jul-97 mby  Removed 'Grayscale output methods' section -- this is
 *                  never used (APP_FUNCTIONS, APP_DEMOGR, APP_BORLANDC).
 *   05-Sep-97 jfd  Moved COMPRESSED_CACHE below the DO NOT CHANGE line.
 *   26-Sep-97 jwd  Moved BOLD_FCO below the DO NOT CHANGE line.
 *   21-Nov-97 mby  For code reduction purposes added new #define's - below
 *                  the line - for FCO_PLUGINS, FCO_CONVERGENTFONT,
 *                  and FCO_CURVESHARE.
 *	 30-Jan-98 slg	Remove obsolete plugin options (below the line) = 
 *					TT_IFPLUG, TT_TTPLUG_IF, TT_TTPLUG_ANY. (Other files
 *					modified = cgif.c, bucket.c, ix.c, chr_def.c)
 *   03-Feb-98 dlk  Added option HP_4000 for HP4000 emulation (0=OFF, 1=ON).
 *   26-Feb-98 dlk  Added additional comments for HP_4000 option.
 *   03-Mar-98 dah  Added AGFATOOLS flag
 *   27-Mar-98 jwd  Removed comment describing 2 as an option for PS_PLUGINS.
 *	 29-May-98 slg  Add below-the-line flag UFST_REENTRANT - this flag will
 *					control whether UFST is built with one global structure
 *					"if_state" (declared within maker.c), or whether it uses
 *					a pointer to an instance of the IF_STATE structure 
 *					(declared within the application).
 *   06-Jul-98 slg	Add below-the-line flag FCO_STANDALONE - set to 1 only to 
 *					build "fcodump2", at this time - replaces no-longer-usable
 *					\rts\fco flag UFST_ON used by "fcodump" utility. 
 *					Also add above-the-line flag USE_SURFER_API - set to 1 to
 *					use Surfer (New Media) API code in \surflib, and related
 *					state variables in "if_state".
 *   08-Jul-98 dlk  Added defines for CFF_DISK and CFF_ROM.  Also addded con-
 *                  ditional defines for CFF_RDR, PST1_RDR, and T1MMASTER to
 *                  be defined (to 1, 'below-the-line') when either CFF_DISK ||
 *                  CFF_ROM is defined.  Deleted T1REMASTER as that code is
 *                  small and will always be compiled in when T1MASTER is ON.
 *                  Also DISK_FONTS, NON_IF_FONT, and ROM are set below-the-
 *                  line when corresponding CFF_DISK or CFF_ROM are set.
 *                  Changed this file's copyright notice dates.
 *   08-Jul-98 dlk  Fixed 'below-the-line' conditional define for T1MMASTER.
 *	 09-Jul-98 slg	Fixed T1MMASTER below-the-line define again, to avoid 
 *					redefine-macro warning messages.
 *   15-Jul-98 tbh  compile switch for Microtype 2 = MM2 (MicroModel)
 *	 12-Aug-98 slg	Remove obsolete flags MULTICALLER, SEM_ACCESS; remove
 *					redundant flag OLDATA (as we have below-the-line OUTLINE);
 *					change comment for MM2 (as code checks for #if MM2, rather
 *					than #ifdef MM2, so MM2 should be defined as 0 or 1).
 *	 13-Aug-98 slg	Oops - put back OLDATA flag - not redundant.
 *	 21-Aug-98 sbm	Added check for 32-bit alignment and HUGE_POINTER_SUPPORT.
 *   24-Aug-98 jfd  Changed all references of MM2 to FCO2.
 *   09-Sep-98 dlk  Added pragma error message for USE_UFST_MAPPING set to 1
 *                  when CFF_DISK or CFF_ROM is set to 1.  Added comments.
 *   10-Sep-98 sbm  Removed PCLEO and PST1 defines, moved FCO_TT below line.
 *   10-Sep-98 sbm  Moved PCLEO and PST1 below line.
 *   10-Sep-98 sbm  Set FCO_CURVE_DATA_RELEASE to 0.
 *   30-Sep-98 jfd  Reset FCO_CURVE_DATA_RELEASE to 1 for release.
 *   11-Dec-98 keb  Added ASIANVERT1 and ASIANVERT2 for use with vertical 
 *                  writing code.
 *	 05-Jan-99 slg	Add below-the-line option to build UFST as Windows DLL.
 *	 18-Jan-99 slg	Modify #pragmas to handle MEM_ALIGN of 7.
 *   18-Feb-99 dlk  Removed pragma warning relating to CFF_RDR and the setting
 *                  of USE_UFST_MAPPING.  CFF processing now uncoupled from PS
 *                  TYPE 1 encoding array use.  Changed asociated comments.
 *   18-Feb-99 dlk  Reset to Standard Configuration settings.
 *   25-Mar=99 keb  Added below-the-line PSEUDO_HALF_WIDTH option for one customer  
 *   29-Mar-99 ks   Set CHAR_HANDLE = 1, CHAR_SIZE = 0.
 *	 10-Jun-99 slg	Add below-the-line TT_SCREENRES flag (for special handling
 *					of very-low-res TT screen fonts). Also permit Surfer build
 *					under Visual C++.
 *	 17-Jan-00 slg	Delete obsolete ASIANVERT, ASIANVERT1, ASIANVERT2 (for keb)
 *	 28-Jan-00 jfd  Moved NO_SYMSET_MAPPING above dotted line.
 *	 02-Feb-00 slg	Add two below-the-line defines: SWP799 (to use alternate 
 *					screen-resolution-optimized rasterizer), SIMULDISKROM
 *					(to enable simultaneous Disk/ROM access for MicroType).
 *					Default values for both = DISABLED, initially.
 *	 02-Feb-00 slg	Get rid of SIMULDISKROM define - simultaneous Disk/ROM
 *					access will always be enabled.
 *   03-Feb-00 awr  Changed SWP799 to WINDCOMP
 *   10-Feb-00 awr  Removed PST1_ROM
 *   28-Sep-00 jfd  Added EMBEDDED_BITMAPS.
 *	 29-Nov-00 slg  Removed lots of obsolete below-the-line options: PCLEO, 
 *					PST1, FCO_TT, CP936_ENCODING, CP949_ENCODING, CP950_ENCODING
 *	 04-Dec-00 slg	Added USE_RAW_GLYPHS option (for TT only); set ASIAN_ENCODING
 *					to 1 if USE_RAW_GLYPHS is enabled (because 2-byte charcodes possible)
 *	 09-May-01 slg	Got rid of below-the-line AGFATOOLS hack (solved problem
 *					with better conditional compiles in our test tools)
 *	 21-May-01 slg	Add PST1_RAM option to Disk/Rom options; also add PST1_RAM
 *					to PST1_RDR and ROM defines 
 *   11-Jun-01 slg  Fix up PLUGINS definition: FCO portion of clause is now
 *					(FCO_RDR && FCO_PLUGINS) rather than FCO_PLUGINS, which is
 *					always true (as it indicates whether, if using MicroType, 
 *					you want to use plugins)
 *  14-jun-01 SWP   above the line define's for NEW_PS_HINTS and HINT_DEBUG
 *	17-Jul-01 slg	move NEW_PS_HINTS and HINT_DEBUG below-the-line (since we
 *					don't want these values to be modified by customers); 
 *					set all defines to match target out-of-box values
 *  05-Dec-01 jfd   Added new directive STIK (for stick font support).
 *                  Added pragma to display message if STIK without TT_DISK,
 *                  TT_ROM or TT_ROM_ACT.
 *  23-Sep-02 jwd   Added configuration directive OPTIMIZE_FCOACCESS (below-the-line) to
 *                  enable/disable performance optimization in CGIFfco_Access().
 *  15-Aug-02 awr   Added DIMM_DISK
 *  25-Sep-02 jfd     Added new define SYMBOL_ENCODING.
 *  17-Oct-02 jfd     Added new define UCS4_ENCODING.
 *	19-Jun-03 slg	Remove obsolete conditions IVPNRUGX and NON_IF_FONT.
 *					Centralize many local / semi-local debug flags (SHOWINPUT,
 *					MEM_DUMP_INT, MEM_TRACE, PRINT_BUCKETS, 
 *					DISPLAY_STATS, ASIAN_HASH_TRACE, 
 *					MM_DIMFILE_DEBUG, MMDECODE_DEBUG, 
 *					GRAYSTATS, GRAYSTATS_1, 
 *					CHECK_MEMORY_GRAY, CHECK_MEMORY_ACT.
 *	26-Apr-04 slg	Add new (below-the-line) option IFBITSPLIT; remove temporary
 *					flag NEW_PS_HINTS (new code is now the only option); remove
 *					FCO_CURVE_DATA_RELEASE flag; replace some automatic setting 
 *					of directives with #pragma tests (FIX_CONTOURS needs CGBITMAP
 *					and CHAR_HANDLE; BOLD and GRAYSCALING both need NON_Z_WIND)
 *			
 */

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

#define GG_CUSTOM         1  /* Switch on customisations to the UFST source */


/*-------------------*/
/* Memory Management */
/*-------------------*/

/* Non-CG (external) memory management can be provided outside of the UFST  */
/* core and referenced via extmem.c by setting INT_MEM_MGT==0. C system     */
/* calls malloc & free can be the basis for external memory management.     */
#define INT_MEM_MGT       0  /* CG (Internal) memory management scheme      */
#define HUGE_PTR_SUPPORT  0  /* support > 64 KB memory blocks in 16-bit DOS */
#define DEFUND            0  /* requires INT_MEM_MGT==1                     */

/*  At least one of CACHE, CHAR_SIZE or CHAR_HANDLE must be set to 1        */
#define CACHE             0  /* CG caching of character bitmaps             */
#define CHAR_HANDLE       1  /* Non-caching character generation            */
#define CHAR_SIZE         1  /* char size info and bitmap generation        */

#define CACHE_BY_REF	  0	/* if CACHE=1, and CACHE_BY_REF=0, use original caching */
							/* if CACHE=1, and CACHE_BY_REF=1, use cache-by-reference-count */

/*------------------------*/
/* Floating Point Options */
/*------------------------*/
/* Non-CG (external) floating point processing can be provided outside of  */
/* the UFST core by setting INT_FP==0.  C system calls to floating point   */
/* functions can provide the same functionality.                           */
#define INT_FP            0   /* CG (internal) math package                */


/*---------------------------------*/
/* Bitmap and Outline Output Modes */
/*---------------------------------*/
#define CGBITMAP          1  /* generate bitmaps                            */
#define LINEAR            1  /* Linear outlines                     [was 0] */
#define QUADRA            0  /* Quadratic outlines                          */
#define CUBIC             1  /* Cubic outlines                      [was 0] */
#define GRAYSCALING       0  /* graymap output                              */

#define OLDATA            1  /* enable CG outline functions         [was 0] */
                             /* required if LINEAR, QUADRA or CUBIC == 1    */

#define TILE              0  /* generate characters in tiles                */

#define SPECIAL_EFFECTS   0  /* Allow generation of special effects, Emboss, Engrave, etc */
                             /* Can NOT be used with TILE or BANDING */

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
/* Disk input                                                               */
#define IF_DISK           0  /* Intellifont disk input                      */
#define PST1_DISK         0  /* PostScript Type 1 disk input                */
#define CFF_DISK          0  /* PostScript CFF disk input                   */
#define TT_DISK           0  /* TrueType disk input                         */
#define FCO_DISK          0  /* FCO disk input                              */
#define T1MMASTER         0  /* type 1 multimaster                          */

#define DIMM_DISK         0  /* font in rom uses disk i/o to access data    */
                             /* currently, this only works for TT_DISK mode */

/* You can only enable one of (FCO1, FCO2, FCO3) in a single build of UFST */
#define FCO1              0		/* use MICROTYPE1 Data Format? (Version 0x13 and below)	*/
#define FCO2              0		/* use MICROTYPE2 Data Format? */
#define FCO3              1		/* use MICROTYPE3 Data Format? (new in UFST 5.0) */

/* PCLEO formatted input                                                    */
#define IF_PCLEOI         1  /* Intellifont PCLEXO input            [was 0] */
/*
NOTE:
   Only 1 form of type 1 downloaded soft font may be active at a time.
   PST1_SFNTI is for the generic downloaded soft fonts.
*/
#define PST1_SFNTI        0  /* PostScript type 1 soft font input           */
#define TT_PCLEOI         1  /* TrueType PCLEXO input               [was 0] */

/* ROM input */
#define IF_ROM            1  /* IF ROM input (Plugins, core fonts in ROM)   */
#define PST1_RAM          0  /* PostScript Type 1 RAM input                 */
#define CFF_ROM           0  /* PostScript CFF ROM input                    */
#define TT_ROM            1  /* TrueType ROM input                  [was 0] */
#define FCO_ROM           1  /* FCO ROM input                               */
#define TT_ROM_ACT        0  /* AGFA Compressed TrueType ROM input          */

/* define NC_REWRITE to activate nested composite processing */
#define NC_REWRITE


/* Embedded bitmap support                                                  */
#define EMBEDDED_BITMAPS  1  /* TT Embedded bitmap input                    */

#define USE_RAW_GLYPHS    0  /* TT raw-glyph-index support (no CMAP table)  */

/*-----------------------*/
/* Miscellaneous Options */
/*-----------------------*/

#define SEGACCESS         0  /* allow independent access to IF segment data
                              * 0 no access
                              * 1 access via font id
                              * 2 access via pathname
                              */

#define FCOACCESS         1  /* allow independent access to FCO segment data
                              * 0 no access
                              * 1 access via pathname & fco_index
                              */

#define WIDTHS            1  /* allow independent access to width data      */
#define BOLD              0  /* allow pseudo emboldening                    */
#define BOLD_P6           1  /* emboldening for PCL 6 emulation     [was 0] */
#define BOLD_HORIZONTAL   0
#define BOLD_VERTICAL     0

#define HP_4000           0  /* allow bucket searching and data scaling
                              * to emulate HP4000 (at 2048 design units).
                              * 0 = NO HP4000 emulation
                              * 1 = enable HP4000 emulation
                              *
                              * For use with FCO_DISK or FCO_ROM.
                              * Also, DU_ESCAPEMENT should be set to 1.
                              */

#define DYNAMIC_FONTS     1  /* can use IF fonts, that are not in IF.FNT block [was 0] */
#define NON_Z_WIND        1  /* specify non-zero winding transitions           */

/*--------------------------------*/
/* Scaling Paramater Input Format */
/*--------------------------------*/
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

/*---------------------------*/
/* Raster Line Organizations */
/*---------------------------*/
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

#define RASTER_ORG    EIGHT_BIT_CHUNK
#define LEFT_TO_RIGHT_BIT_ORDER  1   /* 1 = left-to-right bit ordering  */
                                     /* within chunk, 0 = right-to-left */

/*--------------------------------------------*/
/* Scan Conversion and Output Coordinate size */
/*--------------------------------------------*/
/*  INTR_SIZE controls the precision at which Intelifont outlines are
 *  vectorized and scan converted.  It must be set to either 16 or 32.
 *  The operational limits are 999 point at 300dpi when set to 16 and
 *  999 point at 600dpi when set to 32.  If your processor's integer
 *  length is 32, then it is best for performance to set INTR_SIZE at
 *  32 regardless of your output size requirements.
 */

#define INTR_SIZE        32

/*------------------------*/
/* Font Metrics Precision */
/*------------------------*/
/*  If DU_ESCAPEMENT is set to 1, the escapement field of the character
 *  will be in the design units specified in "du_emx" and "du_emy" of the
 *  IFBITMAP/IFOUTLINE data structures. Thus, "escapement/du_emx" is the
 *  fraction of an em to escape. If DU_ESCAPEMENT is set to 0, then the
 *  escapement will be returned in 8782 units per em as is now.
 *
 */
#define DU_ESCAPEMENT     1  /* Escapement design units:                    */
                             /*  0 == use 8782/em, 1 == use native units    */

/*------------------------*/
/* Banding                */
/*------------------------*/
/*  This option will support caching of horizontal bands of character
 *  bitmaps. By a horizontal band, we mean a tile whose width is the
 *  entire width of the character bitmap.
 */
#define BANDING           0

/*------------------------*/
/* Font Bounding Box      */
/*------------------------*/
/*  If set to 1, this option supports the CGIFbound_box() function that
 *  returns an upper bound estimate of the font bounding box in output pixels.
 */
#define FONTBBOX          0

/*------------------------*/
/* Font Metrics           */
/*------------------------*/
/*  If set to 1, this option enables the function CGIFfont_metrics().
 */
#define FNT_METRICS       1

/* If GET_VERTICAL_METRICS is set to 1, vertical metrics (as well as horizontal
 metrics) will be returned in the IFOUTLINE and IFBITMAP structures. */

#define	GET_VERTICAL_METRICS 0

/* If DIRECT_TT_TABLE_ACCESS is set to 1, TrueType tables in ROM/RAM can be accessed 
directly (with memory pointers),by using the CGIFtt_query_direct() call.
 
Note that, even if DIRECT_TT_TABLE_ACCESS is not set, copies of TrueType tables 
can still be obtained by using the CGIFtt_query() call. */

#define DIRECT_TT_TABLE_ACCESS	0		

/*------------------------------*/
/* Application Generated Output */
/*------------------------------*/
/*  If set to 1, this option lets the app integrate its own character
 *  generation into UFST.
 */
#define ALIEN_BITS        0

/*-------------------------------------------------------------------------
USE_JUMP_TABLES was implemented for systems using overlay managers which
can not properly handle indirect calls through pointers. The default is
to use the jump tables by setting this value = 1. Setting the value = 0
will cause all calls to be made directly. The code size will be slightly
larger and the performance slightly faster.
---------------------------------------------------------------------------*/
#define USE_JUMP_TABLES   1


/*-------------------------------------------------------------------------
The USE_UFST_MAPPING options DO NOT apply to CFF processing.
---------------------------------------------------------------------------*/
#define USE_UFST_MAPPING  1     /* 1-PCL sym sets, 0-encoding array, 2-both */


/*
The following group of constants are used for PostScript Type 0 and
TrueType Asian font support.
*/

/*  These indicate the Font Encodings to support                                          */
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



/* Transcoding Options */
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
                                 */

/*------------------------*/
/* Symbol Set Mapping     */
/*------------------------*/
#define NO_SYMSET_MAPPING 0     /* 0 - use UFST symbol sets for mapping */


/*------------------------*/
/* Stik font processing   */
/*------------------------*/
#define STIK              0     /* 1 - enable stick font processing */
#define CCC               0     /* 1 - enable CCC font processing   */
#define FS_DIRECT		  0	    /* 1 - enable direct drawing into the bitmap */

#define ARABIC_AUTO_HINT  0

/*------------------------*/
/* linked font	          */
/*------------------------*/
#define LINKED_FONT		0		/* 1 - enable linked TT fonts */


/*------------------------*/
/*------------------------*/
/* Contour fixing         */
/*------------------------*/
#define FIX_CONTOURS      1     /* 1 - enable fixing of badly wound contours */

#define TT_SCREENRES      0    /* enable special processing for TT at very small screen resolutions?  */

#define TOL_QUAD_FLAT	  2    /* 1/4 pixel error for rasterization - recommended if TT_SCREENRES=0 */
/* #define TOL_QUAD_FLAT   4  */    /* 1/16 pixel error for rasterization - recommended if TT_SCREENRES=1 */

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



/**************************************************************************/
/**--**--**              DO NOT CHANGE below this line.          **--**--**/

/***** start of private debug-flag section *****/

/* define to enable hint debugging in PostScript code */
#undef HINT_DEBUG

/* for LOTS of debugging information, define AGFADEBUG here, 
	and set if_state.trace_sw by using the UFST_debug_on() API call. */
#undef AGFADEBUG

/* Note that some of the debug options will only print output if the standard
	debug is enabled (i.e., if AGFADEBUG is defined, and if_state.trace_sw is set
	by using the UFST_debug_on() API call) */

#define SHOWINPUT    	0		/* set to 1 to print out Type1 input in t1iscan.c */
#define NZ_DUMP			0    	/* set to 1 to dump lists in nzwind.c */

#define CHECK_MEM_EXT	0		/* set to 1 for external-memory overwrite check */
#define MEM_TRACE   	0      	/* set to 1 to trace MEMalloc() & MEMfree() */
#define PRINT_BUCKETS	0		/* set to 1 to print out all buckets */
#define DISPLAY_STATS   0  		/* set to 1 to display linked-list ptrs in dll.c */
								/* in insert_at_head_of_list(), remove_from_list() */

#define ASIAN_HASH_TRACE  0  	/* set to 1 to trace the Asian Hash Table */

#undef MM_DIMFILE_DEBUG			/* define to enable MT debug in fc_da.c */
#undef MMDECODE_DEBUG			/* define to enable MT debug in fc_intf2.c, fc_da.c */
								/* MMDECODE_DEBUG uses "mmdecode_trace_sw" global vbl */

#define GRAYSTATS   0   		/* set to 1 to enable grayscale #-of-pixels stats */
								/* GRAYSTATS uses externally-defined "char_ct" global vbl */
#define GRAYSTATS_1 0   		/* set to 1 to enable grayscale memory stats */
#define CHECK_MEMORY_GRAY 0  	/* set to 1 for grayscale memory check in graymap.c */

#undef	CHECK_MEMORY_ACT		/* define for ACT memory check in memory.c */

/***** end of debug-flag section *****/

#define MTI_PCLEO_DEMO 1	/* set to 1 to enable use of MTI's sample code for PCLEO processing */
							/* set to 0 if you're implementing your own PCLEO processing */

#define IFBITSPLIT	0		/* split IFBITMAP into 2 structures? (header/bitmap) */

/* define to force align to 32-bit boundaries for global tables in TTPCLEOs */
#define ALIGN_TTPCLEO_TABLES	0

/* should we bring WINDCOMP above-the-line in the next release? */
/* define WINDCOMP to use alternate (screen-resolution-optimized) rasterizer */
#undef WINDCOMP       


#define WIDTH_NOSYMMAP	   0	/* currently-unused variant of CGIFwidth function */
								/* (designed to work with old NO_SYMSET_MAPPING option) */

#define	PS_ERRORCHECK	0	/* if set to 1, enable additional errorchecking of PostScript 
									input (initially, Type1 stackchecking) */

#define USBOUNDBOX     0		/* enable unscaled bounding box option? */
#define THREAD_DUMP    0        /* debug option for multithreading */
#define PRINT_CID_INFO 0		/* debug option for Type1 CID fonts */

/*-----------------------------*/
/* Dependent and Fixed Defines */
/*-----------------------------*/

#define COMPRESSED_CACHE  0
#define BOLD_FCO          0     /* emboldening for MicroType */

/* Max size of bitmap in bytes. Relies on previously defined MAX_BM_BITS */
#define MAX_BM          (UL32)((1 << (UL32)MAX_BM_BITS) - 1)

#define IF_RDR          (IF_DISK || IF_PCLEOI || IF_ROM)
#define PST1_RDR        (PST1_DISK || PST1_RAM || PST1_SFNTI || CFF_DISK || CFF_ROM)
#define CFF_RDR         (CFF_DISK || CFF_ROM)
#define TT_RDR          (TT_DISK || TT_PCLEOI || TT_ROM || TT_ROM_ACT)
#define FCO_RDR         (FCO_DISK || FCO_ROM)

#define PS_RESIDENT		(PST1_DISK || PST1_RAM || CFF_DISK || CFF_ROM)	

#if CFF_RDR
#undef T1MMASTER
#define T1MMASTER 1
#endif 

/* 1-PCL sym sets, 0-encoding array, 2-both */
#define USE_PS_ARRAY	((USE_UFST_MAPPING == 0) || (USE_UFST_MAPPING == 2)) 
#define USE_PS_SYMSET	((USE_UFST_MAPPING == 1) || (USE_UFST_MAPPING == 2))	    

#define FCO_PLUGINS        1        /* Default values are ALWAYS 1 */
#define FCO_CONVERGENTFONT 1
#define FCO_CURVESHARE     1


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

#define MAX_FCOS 3
#define COMPRESSED            (0x01)
#define UNCOMPRESSED          (0x10)

#if FCO2 || FCO3
#define FCO_CURVE_DATA  ( COMPRESSED )
#else
#define FCO_CURVE_DATA  ( UNCOMPRESSED )
#endif

#define FCO_STANDALONE	0	/* set this to 1 ONLY if building "fcodump2" */

#define DISK_FONTS      (IF_DISK || PST1_DISK || CFF_DISK || TT_DISK || FCO_DISK)
#define PCLEO_RDR       (IF_PCLEOI || PST1_SFNTI || TT_PCLEOI)
#define ROM             (IF_ROM || PST1_RAM || CFF_ROM || TT_ROM || FCO_ROM || TT_ROM_ACT)

#define PLUGINS         (IF_RDR || TT_PLUGINS || PS_PLUGINS || (FCO_RDR && FCO_PLUGINS))
#define TT_NOPLUG       (TT_PLUGINS == 0)
#define TT_TTPLUG       (TT_PLUGINS == 2)
#define PS_NOPLUG       (PS_PLUGINS == 0)
#define PS_IFPLUG       (PS_PLUGINS == 1)
#define PS_MTPLUG       (PS_PLUGINS == 2)


/* Set OUTLINE if any of the outline options have been specified.         */
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
                   || BIG5_ENCODING || GB_ENCODING ) && UNICODE_ENCODING)

/*-------------------------------------------------------------------*/
/*       Build-DLL options											 */
/*-------------------------------------------------------------------*/
/* defines for possible values of UFST_LINK_MODE */
#define UFST_STATIC_LINK	0	/* use this to build static-linked UFST libraries/apps */
#define UFST_BUILD_DLL		1	/* use this to build UFST libraries as DLL */
#define UFST_DLL_CLIENT		2	/* use this to build UFST application to use UFST DLL */

/* "UFST_LINK_MODE" should be set to "UFST_STATIC_LINK", unless you are building
a Windows UFST application that uses UFST as a DLL (for example, in a multi-
threaded application). In that case, you must build the UFST DLL with this flag
set to "UFST_BUILD_DLL", but build the application with it set to "UFST_DLL_CLIENT". */
 
#define UFST_LINK_MODE	UFST_STATIC_LINK

/*-------------------------------------------------------------------*/
/*               Test for invalid switch settings                    */
/*-------------------------------------------------------------------*/

#ifndef SUN        /* no #pragma on standard Sun C compiler   -ss 12/4/91 */

#if (DEFUND && !INT_MEM_MGT)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   INT_MEM_MGT must be 1 if DEFUND is 1!")
#pragma message("   ****************************************************\n")
#endif

#if (DEFUND && CACHE && CACHE_BY_REF)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   CACHE_BY_REF option is incompatible with DEFUND!")
#pragma message("   ****************************************************\n")
#endif

#if (!CACHE && !CHAR_SIZE && !CHAR_HANDLE)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   At least one of CACHE, CHAR_SIZE or CHAR_HANDLE must be 1!")
#pragma message("   ****************************************************\n")
#endif

#if (!CGBITMAP && !OUTLINE && !GRAYSCALING)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   At least one of CGBITMAP, OUTLINE or GRAYSCALING must be 1!")
#pragma message("   ****************************************************\n")
#endif

/*
#error CGCONFIG: MEM_ALIGN must be 0, 1, 3, or 7!
*/

/*
#error CGCONFIG: If MEM_ALIGN = 32-bit or 64-bit alignment, HUGE_POINTER_SUPPORT must be off!
*/

#if ((INTR_SIZE != 16) && (INTR_SIZE != 32))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   INTR_SIZE must be defined as 16 or 32")
#pragma message("   ****************************************************\n")
#endif

#if (BANDING && (!TILE || !CACHE))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   TILE and CACHE must be 1 if BANDING is 1!")
#pragma message("   ****************************************************\n")
#endif

#if (SPECIAL_EFFECTS && (TILE || BANDING))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use SPECIAL_EFFECTS with TILE or BANDING")
#pragma message("   ****************************************************\n")
#endif

#if (STIK && !(TT_DISK || TT_ROM || TT_ROM_ACT))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use STIK without TT_DISK, TT_ROM or TT_ROM_ACT")
#pragma message("   ****************************************************\n")
#endif

#if (LINKED_FONT && !(TT_RDR && UNICODE_ENCODING))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use LINKED FONT without TT_RDR and UNICODE_ENCODING turned on")
#pragma message("   ****************************************************\n")
#endif

#if (FIX_CONTOURS && !(CGBITMAP && CHAR_HANDLE))
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use FIX_CONTOURS without CGBITMAP and CHAR_HANDLE turned on")
#pragma message("   ****************************************************\n")
#endif

#if (BOLD && !NON_Z_WIND)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use BOLD without NON_Z_WIND turned on")
#pragma message("   ****************************************************\n")
#endif

#if (GRAYSCALING && !NON_Z_WIND)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use GRAYSCALING without NON_Z_WIND turned on")
#pragma message("   ****************************************************\n")
#endif

#if (IF_ROM && IF_DISK)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use IF_ROM and IF_DISK options together\n")
#pragma message("   ****************************************************\n")
#endif

#if (DISK_FONTS && ROM && PS_IFPLUG)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use PS_IFPLUG option in a mixed disk/ROM configuration\n")
#pragma message("   ****************************************************\n")
#endif

#if (FCO_RDR && ((FCO1 && FCO2) || (FCO1 && FCO3) || (FCO2 && FCO3) || (!FCO1 && !FCO2 && !FCO3)))
#pragma message("\n   *****************************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   When using MicroType, must enable exactly one of FCO1, FCO2, FCO3\n")
#pragma message("   *****************************************************************\n")
#endif

#if (UFST_MULTITHREAD && !UFST_REENTRANT)
#pragma message("\n   ****************************************************")
#pragma message("   Configuration error in CGCONFIG.H :")
#pragma message("   Can not use UFST_MULTITHREAD without UFST_REENTRANT")
#pragma message("   ****************************************************\n")
#endif

#endif  /* !SUN  */

#endif	/* __CGCONFIG__ */

