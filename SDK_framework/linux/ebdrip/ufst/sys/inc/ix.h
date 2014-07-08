/* $HopeName: GGEufst5!sys:inc:ix.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2004 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/sys:inc:ix.h,v 1.3.8.1.1.1 2013/12/19 11:24:03 rogerb Exp $ */
/* $Date: 2013/12/19 11:24:03 $ */
/* ix.h */

/*  01-Apr-90  awr  Expanded SS_DIRECTORY to include pcleo info
 *  12-Apr-90  jfd  Added handles for Typeface Header and Font
 *                    Alias segments to BUCKET structure
 *  07-Jun-90  dah  Added input_filetype to INDEX_ENTRY and BUCKET structs.
 *  01-Jul-90  awr  Corrected comments, added bit flag definitions and
 *                  screen font definitions.
 *  15-Aug-90  awr  added BUCKET's handle to BUCKET structure.
 *  17-Sep-90  jfd  Changed CHARBUFSIZE from 2048 to 4096
 *  02-Jan-91  jfd  Added struct CHR_DEF_HDR
 *  02-Jan 91  ss   Added (ROM defined) lib_offset field to INDEX_ENTRY struct
 *                  Added lib_handle in BUCKET struct
 *                  Added EXPBUFSIZE for expansion buffer size only
 *  08-Jan-91  ss   Changed INDEX_ENTRY to union lib_offset with font_hdr and
 *                  Added pgif field to CHR_DEF for PCLEO_RDR
 *  09 Jan 91  ss   Added B_ISPCLEO, B_ISROM macros
 *  14-Jan-91  awr  Added font_hdr field and ssnum field to FONTINDEX
 *  24-Jan-91  awr  Changed INDEX_ENTRY structure (#if ROM) so that it
 *                  will be LONG-aligned.
 *  29-Jan-91  jfd  Added description for bits 8-9 of format field.
 *  31-Jan-91  dET  Add support for MSC multi-model compilation.
 *  23-Feb-91  awr  Changed bucket sub-segment handles to offsets.
 *  19-May-91  awr  Changed CHARBUFSIZE to 6000
 *  14 May 91  ss   Added support for new plugin scheme w/ 15 buckets
 *                   at 3 levels of limited sensitive
 *  19-May-91  awr  Changed CHARBUFSIZE to 6000
 *   3 Jun 91  ss   Added SLIM_FONTS mirror flags to CHR_DEF_HDR struct.
 *   5-Jun-91  jfd  Changed HUGE define to MSHUGE to avoid conflict 
 *                  w/Sun system define
 *                  (FIX FROM EARLIER VERSION)
 *   6-Jun-91  jfd  In macros CD_ISPCLEO and CD_ISROM, changed FILETYPE_MASK
 *                  to FC_FILETYPE_MASK
 *   5 Jun 91  ss   Added Fixed Pitch defines.
 *                  Deleted COMPRESS define, no longer needed.
 *  19 Jun 91  ss   Cleaned up defines not used. Added MIRROR mask defines.
 *                  Changed CHR_DEF element name from bucknum to
 *                  buck_search_lvl
 *  21 Jun 91  ss   Moved prototype for fgseg() to modules that actually
 *                  use it due to problem with prototypes on SUN3.
 *  24 Jun 91  ss   Changed PBYTE fontindex.pfnt_index to PUBYTE.
 *                  Changed PBYTE bucket.plibfont to PUBYTE.
 *   4-Jul-91  awr  changed types in CHR_DEF to WORDVECTOR
 *  10 Aug 91  ss   Changed CHR_DEF_HDR.mirror flags to one word.
 *                  Added and corrected defines for CHR_DEF_HDR.mirror_flags.
 *  16 Sep 91  ss   Added font_hdr field in CHR_DEF_HDR for PCLEO_RDR and disk
 *   1 Oct 91  ss   Made EXPBUFSIZE larger.
 *   7 Oct 91  jfd  Changed typedef statements for PCHR_DEF and PCHR_DEF_HDR
 *                  to "... FARPTR * ... ".
 *  01-Nov-91  jfd  Changed CHARBUFSIZE to from 6000 to 4096.
 *   7 Nov 91  ss   Conditionally compiled EXPBUFSIZE cuz SUN seems to need
 *                  a bigger buffer.
 *   3-Jan-92  awr  Removed font_open field from BUCKET
 *   8-Jan-92  rs   Modify BUCKET for the unions (ps, tt, if).
 *  13-Jan-92  rs   Add ptr to charstring to PS_BUCK when char is selected.
 *                  Stored in 'VOID *pthis_charstring'. Add 'fontmatrix[6]'
 *                  to PS_BUCK.
 *  14-Jan-92  rs   Add 'CHAR_STATS' struct definition.
 *  22-Jan-92  rs   Set up 'IF_FUNC_TBL' & 'IF_RAS_TBL' structures to call
 *                  font + rasterizer functions through ptrs.
 *  23-Jan-92  rs   Add 'overshoot' to ps bucket.
 *  28-Jan-92  rs   Add 'PIF_FUNC_TBL pft' to external font bucket.
 *  22-Jan-92  mby  Defined TT_BUCK structure.
 *  31-Jan-92  mby  Added emResolution to TT_BUCK.
 *  06-Feb-92  rs   Add 'last_seek', 'last_key' & 'curr_char' to 'ps_buck'.
 *  10-Feb-92  awr  Added "locked" field to BUCKET structure.
 *  26-Feb-92  bg   Changed EXPBUFSIZE (ifndef SUN) from 700 to 2096
 *                  (4096 - 2000).
 *   4 Mar 92  ss   Added !LINT_ARGS prototypes for functions in structs
 *                  IF_FUNC_TBL and IF_RAS_TBL.
 *   7-Mar-92  awr  added compound character design unit bounding box
 *                  to if_state.
 *   8-Mar-92  awr  added has_path field to BUCKET
 *  13-Mar-92  rs   Change 'last_key' in PS_BUCK from ULONG to UWORD
 *  17-Mar-92  mby  declared elements of TT_BUCK as MEM_HANDLE instead of
 *                  pointers.
 *  02-Apr-92  rs   Portability cleanup (see port.h).
 *  06-Apr-92  rs   Change 'FIXED_PITCH' to 'CGFIXED_PITCH' (portability).
 *  20-Apr-92  awr  Added pix_bound field to CHR_DEF structure
 *  11 May 92  ss   Changed FONTINDEX.small_plugins to plugin_set to use
 *                  as a flag for different plugin sets that can be loaded
 *                  in addition to the small_plugin set.
 *  21-May-92  jfd  Added prototypes for outline functions to support
 *                  function table.
 *                  Changed function prototype for bmras_start_char()
 *                  and _olstartchar() so that argument is PVOID.
 *  21 May 92  ss   Cleaned up references to Letter Gothic to implement
 *                  the plugins now available.
 *  29 May 92  jfd  Disabled old "CHAR_STATS" structure. Replaced with
 *                  "CHAR_STATS" which is combination of old CHAR_STATS 
 *                  and OLSTATS.
 *  17-Jun-92  awr  Changed CHAR_STATS.olbox to an INTRBOX type
 *  24-Jun-92  mby  Modified TT_BUCK structure for PCLEO support
 *   6-Jul-92  jfd  Added "get_width" to IF_FUNC_TBL structure.
 *   6-Jul-92  mby  Removed GlobalTTPtr from TT_BUCK structure.
 *  21-Jul-92  awr  Conditional compile changes.
 *  16-Aug-92  rs   Fix typo for 'USE_UFST_MAPPING'.
 *  23-Sep-92  awr  Added "platID" & "specID" to TT_BUCK.
 *                  Removed media in FONTINDEX and BUCKET
 *                  Changed CHR_DEF.buck_search_lvl to int.
 *                  Changed FONTINDEX.num_searches to int.
 *                  Added font_type field in FONTCONTEXT and BUCKET
 *  14-Oct-92  awr  Added cur_ssnum filed to BUCKET
 *  15-Oct-92  mby  Added "FONTCONTEXT TTfc" to TT_BUCK.
 *  25-Jan-93  jfd  Added fields "du_emx" and "du_emy" to CHAR_STATS 
 *                  structure
 *  04-Feb-93  mby  Added BBoxRes structure. Added get_FontBBox() function
 *                  prototype declarations (FONTBBOX must be set)
 *  02-Mar-93  rs   Add StemSnapV[] & StemSnapH[] to PS_BUCK.
 *  15-Mar-93  rs   Modify PS_BUCK for type 0 fonts.
 *  27-Mar-93  rs   Add define for 'KANJI_ENCODING'.
 *  30-Mar-93  rs   Make CHARBUFSIZE 6000 if 'KANJI_ENCODING'.
 *  06-Apr-93  rs   Make CHARBUFSIZE 8000 if 'KANJI_ENCODING' (there were
 *                  still 4 chars which could not rasterize).
 *  12-Apr-93  mby  Changes for DYNAMIC_FONTS. Added "dynIFActive" flag to
 *                  BUCKET. Added "rom_font_ptr" and "dynix" to fontindex.
 *  21-Apr-93  mby  Remove rom_font_ptr from fontindex structure.
 *  14-May-93  rs   Pipeline upgrade - type 1 quality improvements.
 *  03-Jun-93  jfd  In PS_BUCK struct, changed conditional compile statement
 *                  for hencodearray to "#if !USE_UFST_MAPPING || 
 *                  (USE_UFST_MAPPING == 2)"
 *  06-Jun-93  rs   MultiMaster support 'T1MMASTER'.
 *  15-Jun-93  rs   Add 'hexascii' to type 1 bucket.
 *  12-Jul-93  mby  Added noTFS to FONTINDEX for CONVERGENT_FONTS.
 *                  Added plugintt_pix to FONTINDEX for TT_PLUG_IF.
 *                  Use bit 6 in bucket_num (MULTIFORMAT only). UFST uses
 *                  this to decide if plugin font type is IF or TT. 
 *  15-Jul-93  mby  Moved CONVERGENT_FONT #defines and AWT_TABLE structure
 *                  into this module.
 *  23-Jul-93  awr  Added tran_run_ct field to CHAR_STATS structure to
 *                  support non-zero winding bitmap fill
 *  02-Aug-93  jfd  In PS_BUCK structure, added field "ocharnames" (offset
 *                  to char names table) based on PST1_ROM.
 *  12-Aug-93  mby  Added 'awtsub_on' to FONTINDEX structure for CONVERGENT_FONTS.
 *                  Reorganized AWT_TABLE structure.
 *  26-Aug-93 maib  Added spotsize field to TT_BUCK struct.
 *  26-Aug-93  mby  Changed AWT_ID to 'A' 'W' 'T' '2'.
 *  23-Sep-93  mby  Replaced fontHndl by diskHndl & ROMHndl in TT_BUCK.
 *                  Put conditional compiles in TT_BUCK; added ROM1 support.
 *                  Added BUCKET.usePlugins.
 *                  Made ifxxx() function declarations conditional on IF_RDR.
 *  29-Sep-93  jfd  Added field "FontBBox[4]" to PS_BUCK structure.
 *  01-Oct-93  mby  Added "scaleFactor" to AWT_TABLE structure.
 *                  Added FontBBox[4] to TT_BUCK.
 *  05-Oct-93  mby  Added #defines CVF_NO_INDEX, CVF_REQU_FNT, CVF_SUBST_FNT
 *  01-Dec-93  jfd  Changed KANJI_ENCODING to ASIAN_ENCODING.
 *  06-Jan-94  mby  Changed AWT_ID to 'A' 'W' 'T' '3'.
 *  03-Feb-94  mby  Convergent font changes. 'AWT3' -> 'AWT4'. Moved awtsub_on
 *                  to IF_STATE (see if_type.h). Added font italic angle,
 *                  horizontal offset data, and prohibit italic information
 *                  to AWT_TABLE structure.
 *  06-Mar-94  mby  Convergent fonts: Build UFST to use either AWT3 or AWT4
 *                  format. AWT_ID -> AWT_ID3 and AWT_ID4. Part of AWT_TABLE
 *                  is conditionally compiled.
 *  22-Mar-94  jfd  In structure PS_BUCK, added fields hCOD_Offset and
 *                  hCSA_Offset for ASIAN ROM support.
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * FCO Changes from 1.27.1.3:
 *  14-Mar-94  mby  Added function prototypes for pointer callable "fco_"
 *                   UFST functions - fco_load_font, etc.
 *                  Added FCO_BUCK structure inside BUCKET.
 *                  Added FCObject to FONTINDEX structure.
 *  26-May-94  mby  Removed FCObject from FONTINDEX. It's now in IF_STATE.
 *  03-Jun-94  mby  Added 'design_EmRes' to FCO_BUCK.
 *  03-Aug-94  mby  Added 'fcoChar' and 'charCloseFlag' to FCO_BUCK.
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 *  31-Aug-94  mby  Added 'spaceBand' and 'compositionFlag' to FCO_BUCK.
 *  02-Sep-94  mby  Added fcoSearchPath_on, fcoSearchPath, fcoPlugHndl to
 *                  FONTINDEX. They're used in BUCKfind().
 *  13-Sep-94  mby  For outline sharing, added MEM_HANDLE fcoFontSh to FCO_BUCK.
 *  21-Sep-94  mby  Added 'FONTCONTEXT fcoFC' to FCO_BUCK structure.
 *  13-Oct-94  mby  Added 'orThreshold' to FCO_BUCK.
 *  11-Dec-94  mby  Changed SW16VECTOR CHAR_STATS.origin to
 *                  SL32VECTOR CHAR_STATS.origin_cs to fix 16-bit origin
 *                  overflow at very large sizes.
 *  09-Feb-95  jfd  In FCO_BUCK structure, added fields ThinSpace, ENSpace
 *                  and EMSpace.
 *  26-Feb-95  jfd  In DISK version of INDEX_ENTRY structure, if WATCOM,
 *                  added pad word following "name_off" field to resolve
 *                  alignment problem introduced when NAT_ALIGN was
 *                  changed from 2 to 4 for WATCOM.
 *  24-May-95  mby  Added FCO_BUCK.oblMatrix[] obliqued transformation matrix
 *                  for Convergent Fonts. Added FCO_BUCK.xbold, ybold
 *                  for experimental pseudobold.
 *  23-May-95  mby  Add argument to declaration of fco_get_FontBBox().
 *  01-Jun-95  mby  Removed "fcoFontSh" from FCO_BUCK structure.
 *  14-Jun-95  mby  Added FCO_BUCK.identityMatrix. In BUCKET.p union,
 *                  conditionally compiled, "in", "ps", "tt", "fco".
 *  26-Jul-95  mby  Added 'FCO_BUCK.matrixType' for orthogonal rotation support.
 *  23-Jan-96  mby  Added cvfChangeScale, cvfScale1, cvScale2 to FCO_BUCK.
 *  13-Jun-96  jwd  Expanded meaning of FONTINDEX.font_type, to include font
 *                  input bits from FONTCONTEXT.ExtendFlags member. Changed
 *                  meaning of BUCKET.extern_font to int from BOOLEAN, due to
 *                  different TT external font formats; we need to distinguish
 *                  between format 15 & format 16 TT PCLETTOs. Added Vert Subs
 *                  data  items to the TT_BUCK structure,  to handle format 16
 *                  downloads containing a Vertical Substitution table.
 *  26-Aug-96  mby  Replaced #if PSEUDO_BOLD_FCO with #if BOLD_FCO (MicroType emboldening).
 *  26-Aug-96  mby  PCL 6 emulation support:
 *                  - In bucket structure, removed "rom_font"; added
 *                  "fc_format". This is used for looking at both the
 *                  ROM/DISK and PCL6 bits rather than storing them as
 *                  separate items in the structure.
 *                  - Added 1 to fcoSearchPath[] array for the new plugin
 *                  search algorithm.
 *                  - removed fontindex.fcoPlugHndl ( => if_state.FCO_PluginH)
 *                  - fontindex.fcoMakeNewSrchPath replaces "fcoSearchPath_on"
 *                  - added fontindex.fcoSearchPathType
 *  01-Nov-96  mby  Added function prototypes for xx_get_FontMetrics().
 *  18-Nov-96  mby  In TT_BUCK structure, added "FontBBox" and "emResolution"
 *                  if FNT_METRICS is on.
 *  09-Jan-97  dlk  Added langId to TT_BUCK structure to help distinguish GB
 *                  from BIG5 Asian encoded fonts.
 *  10-Jan-97  dlk  Deleted SCREEN_FONT option as part of project to trim ufst.
 *  13-Jan-97  dlk  Removed CONVERGENT_FONTS option as part of project to trim
 *                  ufst.  Consequently, removed noTFS and awtsub_on from
 *                  FONTINDEX structure, and removed AWT_TABLE.
 *  11-Mar-97  mby  Removed fontindex.fcoSearchPathType (see 8/26/96)
 *  12-Mar-97  dlk  Added GalleyTTHndl, and GalleyTTSize to TT_BUCK structure
 *                  support Galley Character Segment processing in format 15 &
 *                  format 16 PCLETTOs (when they occur).
 *  24-Mar-97  dlk  Changed name of GalleyTTHndl and GalleyTTSize in TT_BUCK
 *                  structure to GalleySegHndl and GalleySegSize respectively.
 *  24-Mar-97  mby  In TT_BUCK, conditionally compiled 'GalleySegHndl' and
 *                  'GalleySegSize' with TT_PCLEOI.
 *  25-Mar-97  dlk  Updated comment for 'GalleySegSize'.
 *  19-May-97  dlk  Changed 'GalleySegHndl' to a pointer 'LPUB8 pGalleySegStart'.
 *  29-May-97  sbm  Add TT_ROM_ACT functionality.
 *  15-Jul-97  dlk  Changed 'pGalleySegStart' pointer back to 'GalleySegHndl'
 *                  memory handle now that memory error has been found in 
 *                  'pcleomgt.c', and not here.
 *  19-Sep-97  mby  Added "FontState" to FCO_BUCK structure.
 *  31-Oct-97  dlk  Added 'GSUB' support fields to TT_BUCK structure (condit-
 *                  ionally compiled for ASIAN_ENCODING.  This is only for
 *                  support of Vertical substitution ('vert' feature).
 *  21-Nov-97  mby  Made 'oblMatrix', 'cvfChangeScale', 'cvfScale1',
 *                  'cvfScale2' members of FCO_BUCK conditional.
 *  08-Dec-97  dlk  Added conditionals (TT_DISK, TT_ROM1, TT_ROM, and TT_ROM_
 *                  ACT to TT_BUCK structure for 'GSUB' Vertical Substitution
 *                  capabilities.
 *  29-Jan-98   awr Added very large character support in CHAR_STATS
 *  03-Feb-98  dlk  Added fc_ExtndFlags to BUCKET struct for HP4000 emulation.
 *	10-Feb-98  slg	pathname shouldn't be in FONTINDEX or BUCKET structs if ROM
 *	09-Mar-98  slg	long last_seek => SL32 last_seek
 *  15-Apr-98  keb  redefined pathname in BUCKET structure
 *	12-Jun-98  slg	Move fn prototypes to shareinc.h
 *  09-Jul-98  dlk  Integrated new CFF processing support (by dek) into UFST
 *                  code base.  Changed copyright notice dates.
 *  29-Jul-98  dlk  Added fields to PS_BUCK str for T1 and CFF MM.
 *  31-Aug-98  keb  modified for xl2.0 font processing support
 *  08-Dec-98  jwd  Upgraded CHARBUFSIZE value for CFF to 8000, to ensure 
 *                  enough memory available for raw character data.
 *  11-Dec-98  keb  Added code to support ASIANVERT for XLfonts
 *  20-Jan-99  dlk  Added 'hcharsetarray', and then 'charset' to 'cft'
 *                  structure for support of PS CFF glyph access by SID.
 *  25-Jan-99  jwd  Revised conditionals for Vertical Substitution variables to 
 *                  allow FORMAT16 and ASIAN_ENCODING independence.
 *  29-Jan-99  dlk  Added 'hcffencodearray' to PS_BUCK.
 *  16-Feb-99  ks   Added 'V' intellifont format library definition V_LIB 
 *  18-Feb-99  dlk  Un-coupled CFF hcffencodearray and CFF hcharsetarray from
 *                  USE_UFST_MAPPING compile time logic - in PS_BUCK structure.
 *  07-Jun-99  jfd  Conditionally compiled definitions for MAXSTEMS and
 *                  MAXWEIGHTS based on #ifndef to resolve compiler warnings.
 *  15-Nov-99  slg	Add 'ttc_index' and 'fontStartOffset' fields to TT_BUCK
 *					structure (for TrueTypeCollections processing)
 *	24-Jan-00  slg	Vertical-writing changes (for keb), plus TT cleanup:
 *					add baseGlyphAW, font_type_TT, xl_char_class, FONT_TYPE_*
 *					defines; delete lookInROM, entityACT, top_bearing.  
 *	26-Jan-00  slg	Define "FontBBox" and "emResolution" unconditionally in
 *					TT_BUCK, since they are used by the vertical-writing code.
 *  10-Feb-00  awr  Removed PST1_ROM
 *  14-Apr-00  keb  Added advanceHeight to CHAR_STATS to support vertical writing
 *  28-Sep-00  jfd  Added new embedded bitmap data structures;
 *                  In TT_BUCK, added EMBEDDED_BITMAP_FONT and EMBEDDED_BITMAP
 *                  fields and added flag to indicate whether bitmap tables
 *                  are embedded in the font.
 *  19-Oct-00 jfd   In EMBEDDED_BITMAP_FONT struct, removed EBLCTableHndl
 *                  and EBDTTableHndl fields; added EBLCOffset, EBDTOffset,
 *                  bst_hndl, ista_hndl and ist_hndl fields.
 *                  In EMBEDDED_BITMAP struct, removed embedded_bitmap_size
 *                  field; added emb_bmp_hndl field.
 *  13-Nov-00 jfd   Added 'CMAP_offset' field to TT_BUCK structure.
 *	04-May-01 slg	Non-UFST data-type cleanup; get rid of AGFATOOLS hack;
 *					tighten up conditional compile for embedded bitmaps.
 *  23-May-01 jfd   OpenType changes:
 *                  Added macros B_OSOTCFF() and GET_pBUCKET().
 *                  Added fields CFFTableOffset, CFFTableSize and private_DICT_offset
 *                  to cft structure.
 *                  Added fields CFFTableOffset and CFFTableSize to TT_BUCK structure.
 *                  Added fields potcff and otcff_font to BUCKET structure.
 *  24-May-01 swp	Lots of changes in the way hints, bluevalues, and standard
 *					stems are stored and processed.  Enabled by #define NEW_PS_HINTS
 *  15-Aug-02 awr   Added DIMM_DISK
 *  29-Oct-02  jfd    In INDEXSUBTABLE structure, replaced pointer variables with arrays to resolve ALPHA
 *                         alignment problems.
*/

#ifndef __IX__
#define __IX__


#define MAX_CC_PARTS  10    /* maximum number of parts in a compound ch. */

#if ASIAN_ENCODING || CFF_RDR         /* add CFF, 12/8/98 jwd  */
#define CHARBUFSIZE  9000             /* character buffer size */
#else
#define CHARBUFSIZE  4096             /* character buffer size */
#endif

/* Bucket Types */
#define  TF_SENSITIVE    0      /* Always the first bucket in icur_index */
#define  TF_PLUGINS      1      /* Not TF_SENSITIVE. jwd, 02/09/06.      */
#define  CREATE_BUCKET   0      /* Argument for BUCKfind()               */
#define  PURGE_BUCKET    1      /*    "      "      "                    */

/*  Typeface number definitions for generic screen faces and plugin faces */
#define FACE_S_SCR       5500L  /* serif screen font                     */
#define FACE_SS_SCR      5501L  /* sans serif screen font                */


/*--------------------------------*/
/*       font index entry         */
/*--------------------------------*/

/*  Definitions and macros for the file type bits within bucket_num   */

#define EXTERN_TYPE    0x0400   /* External vs. Internal              */
#define ROM_TYPE       0x0800   /* ROM based file vs. Disk            */

#define FILEORG_MASK   0x3000   /* file org bits                      */
#define D_LIB          0x0000   /* "D" standard typeface library      */
#define S_LIB          0x1000   /* "S" slim library                   */
#define U_LIB          0x2000   /* "U" ultraslim library              */
#define V_LIB		   0x4000	/* "V" library   ks - 15/02/99  */

/* These macros can be used for both INDEX_ENTRY & BUCKET bucket_num fields */

#define B_ISROM(b)   ((b)->bucket_num & ROM_TYPE)
#define B_ISEXTERN(b) ((b)->bucket_num & EXTERN_TYPE)
#define B_U_LIB(b)   (( (b)->bucket_num & FILEORG_MASK ) == U_LIB)

#define B_LS_MASK       0x0007     /* low 3 bits of bucket_num is lim sens */
#define B_BOLD          0x0001
#define B_SANS_SERIF    0x0002
#define B_ITALIC        0x0004

/* Removed CGFIXED_PITCH dependency from the following constants, to make */
/* them available for PostScript as well as TrueType. Talk to Sandra.     */
/* jwd, 12/7/05.   */

/* #if CGFIXED_PITCH                                 added  -ss 6/5/91    */

#define NFP_BUCKET_LEVELS    3
#define B_FP_PLUGIN_MASK     0x0038   /* 3 fixed pitch bits in IX.bucknum */
#define B_FIXED_PITCH        0x0008   /* FP, no plugins                   */
#define B_COURIER            0x0010   /* courier plugins                  */
#define B_LETTER_GOTHIC      0x0020   /* letter gothic plugins            */

#define COURIER_WIDTH        5291
#define LETTER_GOTHIC_WIDTH  4409
#define B_ISFPITCH(b) ((b)->bucket_num & B_FP_PLUGIN_MASK )

/* #endif CGFIXED_PITCH                                                   */

#if TT_RDR && CFF_RDR	/* 05-23-01 jfd */
#define B_ISOTCFF(b)  ((b)->otcff_font)
#define GET_pBUCKET(b) ( B_ISOTCFF(b) ? (b)->potcff : b )
#else
#define GET_pBUCKET(b) ( b )
#endif	/* TT_RDR && CFF_RDR */

#define B_PLUGIN_TYPE_TT     0x0040   /* for MULTIFORMAT in cgconfig.h */

#if IF_ROM
typedef struct
{
    SL32    tfnum;        /* typeface number                          */
    UL32   fhoff;        /* offset in library file for face header   */
   union {
      UL32  lib_offset;  /* offset to appropriate lib within a font block  */
      LPUB8 font_hdr;    /*  Used for PCLEO - pointer to font header       */
   } p_r;  /* pcleo-rom pointers to beginning of lib or pcleo data          */

    UW16   fhcount;      /* BYTE count of face header                */
    SW16    bucket_num;   /* Bit flags
                           *
                           * Bit 15      Compressed Library Flag
                           *  Value:
                           *    0    Normal library file
                           *    1    Compressed library file
                           *
                           * Bit 10  Internal / External flag
                           *
                           *    0    (0x0000)  Internal font
                           *    1    (0x0400)  PCLEO (external) font
                           *
                           * Bit 11  Physical media for internal fonts
                           *
                           *    0    (0x0000)  Disk based
                           *    1    (0x0800)  ROM based
                           *
                           * Bits 12-13  Font data organization
                           *
                           *    0    (0x0000)  "D" standard library file
                           *    1    (0x1000)  "S" slim font library
                           *    2    (0x2000)  "U" ultra-slim font library
                           *
                           * Bits 9-7    Reserved
                           *
                           * Bit 6   Used only #if MULTIFORMAT = 1
                           *    0      Use IF compatible plugins
                           *    1      Use TT compatible plugins
                           *
                           * Bits 5-3    Fixed Pitch plugin description
                           *   Value   Meaning
                           * (3 bits)
                           *    000     Proportional
                           *  >0 is a fixed pitch typeface
                           *    001     no plugins, all chars in TFS
                           *    010     Courier plugins, pitch =
                           *    100     Letter Gothic plugins, pitch =
                           *
                           *
                           * Bits 2-0  Typeface characteristics
                           *      bit/value    0           1     
                           *       0         normal      bold
                           *       1         serif       sans-serif
                           *       2         non-italic  italic
                           *
                           */
} INDEX_ENTRY;
#else
typedef struct
{
    SL32    tfnum;        /* typeface number                          */
    UW16   name_off;     /* offset to full path name of library file */
#if defined(__WATCOMC__)
    UW16   watpad;       /* pad word for WATCOM only */
#endif
    UL32   fhoff;        /* offset in library file for face header   */
    UW16   fhcount;      /* BYTE count of face header                */
    SW16    bucket_num;   /* Bit flags 
                           *   SAME AS bucket_num description ABOVE.  */
} INDEX_ENTRY;
#endif

typedef INDEX_ENTRY FARPTR * PINDEX_ENTRY;


/*--------------------------------*/
/*     FONTINDEX                  */
/*--------------------------------*/

/*  Defines for Limited Sensitive Buckets                                 */
#define NPSW_FACES           8     /* # of posture/serif/weight LS faces  */
#define NSW_FACES            4     /* # of serif/weight LS faces          */
#define NW_FACES             2     /* # of weight only LS faces           */

#if SLIM_FONTS
#define NBUCKET_LEVELS       5
#define NMAX_PLUGINS        15     /* 14 lim sensitive, universal         */

#define MIRROR_MASK          0x7000
#define ROT90_MASK           (0x1000 >> 12)
#define MIRROR_X_MASK        (0x2000 >> 12)
#define MIRROR_Y_MASK        (0x4000 >> 12)
#define NPARTS_MASK          0x07FF
                                  /* shift over to fit in a byte -ss 8/12/91*/
#define MIRROR_FLAGS( num_parts )  ( (num_parts & MIRROR_MASK) >> 12 )
#define NUM_PARTS( num_parts )     (  num_parts & NPARTS_MASK )

/* defines for mirror flag mapping used in comp_pix.c                     */
#define MIRROR_ROT0          0
#define MIRROR_ROT90         1
#define MIRROR_ROT180        6
#define MIRROR_ROT270        7
#define MIRROR_NEGROT0       2
#define MIRROR_NEGROT90      3
#define MIRROR_NEGROT180     4
#define MIRROR_NEGROT270     5
#else
#define NBUCKET_LEVELS       3
#define NMAX_PLUGINS         5     /* 4 lim sensitive, universal          */
#endif  /* SLIM_FONTS  */

#if CGFIXED_PITCH
#define NFP_PLUGINS          3 /* exact # of plugins for each FP typeface */
#endif
#if FCO_RDR
#define FCO_NPLUGIN_LEVELS   4
#endif

typedef struct
{
#if IF_ROM
    LPUB8       pfnt_index;      /* pointer: font index buffer            */
#else
    MEM_HANDLE  hfnt_index;      /* handle font index buffer              */
#endif

    SW16        entry_ct;        /* number of entries in index            */
    UW16        olibnames;       /* path names of library files in index  */

  /*  The current typeface. */

    SL32         font_type;       /* FONTCONTEXT format field bits 10-13;  */
                                 /* In addition,  for Asian TT, Format 16 */
                                 /* support, ExtendFlags, bits 0-3.       */

    LPUB8       cur_font_hdr;    /* current pcleo header                 */
#if 0	/* 02-27-06  BUCKET-related changes */
    UW16        cur_ssnum;       /* current symbol set number            */
#endif	/* 02-27-06  BUCKET-related changes */
    SL32        cur_tf_num;      /* current type face number             */
    PINDEX_ENTRY cur_pix;        /* TFS INDEX_ENTRY                      */
    BOOLEAN		has_path;
#if DISK_FONTS
    PATHNAME    pathname;
#endif
#if DYNAMIC_FONTS && IF_RDR
    INDEX_ENTRY dynix;           /* passes info for dynamically loaded IF */
#endif

#if PLUGINS
    SL32         plug_type;
#endif

    PINDEX_ENTRY pix;            /* INDEX_ENTRY for search level */
    SL32          num_searches;   /* # searches possible. TFS + LS + U      */
                                 /* PS,TT have an extra level for core.    */

    UW16        icur_index[NBUCKET_LEVELS]; /* Indexes to the buckets of the
                                             * current typeface.
                                             */
                                   /*  indexes of plugin INDEX_ENTRY's    */
    UW16        iplugin[NMAX_PLUGINS]; 
    SW16	    plugin_set;       /*  Describes the set of plugins loaded.
                                   *  #defines given in ix.c
                                   */

#if CGFIXED_PITCH
    UW16        courier_iplugin[ NFP_PLUGINS ];  /*  Courier plugins     */
    UW16        lgothic_iplugin[ NFP_PLUGINS ];  /* LetterGothic plugins */
#endif

    SL32 icore[8];     /* font index indices to core fonts. This arrary is
                       *  parallel to core_list[] in ix.c. It is used to
                       *  substitute the core 8 for PS and TT.
                       */
#if FCO_RDR
    UB8   fcoMakeNewSrchPath;   /* if nonzero, call fco_pluginSearchPath() */
    UB8   fcoSearchPath[FCO_NPLUGIN_LEVELS+1];
                                /* indices of plugin faces */
#endif

} FONTINDEX;
typedef FONTINDEX FARPTR * PFONTINDEX;

typedef MEM_HANDLE HBUCKET;

/*-------------------------------*/
/*    union stuff                */
/*-------------------------------*/
typedef struct
{
    UL32 items;	/* count of items indexed by header                        */
    UL32 step;	/* bytes per seek(offset from 0); also, zbase = &stt.      */
    UL32 stt;	/* &step, stt is the base for offset to data offset pointer*/
    UL32 zbase; /* base for offset to next index on chained indexes        */  
} cfix;
typedef struct
{
    UL32 headsize;
    UL32 aos;		/* absolute offset size: bytes to hold pos from top of file */
    UL32 name;			/* type face name index					*/
	UL32 CFFTableOffset;/* offset to CFF table in disk/ROM font */     /* 05-23-01 jfd */
	UL32 CFFTableSize;  /* size of CFF table in disk/ROM font   */     /* 05-23-01 jfd */
	UL32 private_DICT_offset;	/* offset to start of private DICT */  /* 05-23-01 jfd */
    cfix fonts;
    cfix dict;
    cfix strings;
    cfix globalsubrs;
    cfix charset;       /* pos to start of charset (SIDs) - NOT a full cfix  */
    cfix encoding;      /* pos to start of encoding - NOT a full cfix        */
    cfix charstrings;	/* pos to start of charstrings			*/
    cfix localsubrs;    /* pos to start of local subroutines.	*/
	cfix fontdict;		/* pos to start of FontDict Index (CID only) */
    FILECHAR nameInSet[CFFNAMELEN]; /* name of font in the font set */
} cft;

typedef  LPSB8  PFONT_DES; /* ptr to font path name */

#ifndef MAXSTEMS          /* 06-07-99 jfd        */
#define MAXSTEMS    12    /* from the White Book */
#endif                    /* 06-07-99 jfd        */

#ifndef MAXWEIGHTS        /* 06-07-99 jfd        */
#define MAXWEIGHTS  16    /* from Pipeline       */
#endif                    /* 06-07-99 jfd        */

#define BOTBLUES    12    /* from the White Book */
#define TOPBLUES    12    /* from the White Book */
#define MAXAXES      4    /* four design axes */

typedef struct {
  SL32  intval;  /* or size of string or name */
  FPNUM  realval;
  SW16  type;
  SW16  len;
} TOKENSTR;

/* The following structure "psi_wrk" pulls together lots of working vbls used
in /psi, with not-very-unique names like "x" and "miny". This technique should
probably be used more often, in order to make sense of the mass of newly-global
variables within "if_type". (Sandra, 26 May 98) */

typedef struct {

	TOKENSTR elt;

/*  5/14/93 - rs
   The following variables were move from 'type1char()' in order to
   integrate the Pipeline upgrade. They are now shared among multiple funcs.
*/
		SL32  ex;
		SL32  ey;
		SL32  epx;
		SL32  epy;
		SL32  curx; /* init to 0 - 4/14/93 - rs */
		SL32  cury; /* init to 0 - 4/14/93 - rs */
		SL32  x; /* init to 0 - 4/14/93 - rs */
		SL32  y; /* init to 0 - 4/14/93 - rs */

/* MLOCALs moved here from /psi/t1ihints.c */
		SL32 startx;
		SL32 starty;
		SL32 minx;
		SL32 miny;
		SL32 maxx;
		SL32 maxy;
/*
Add following vbls to interface w/'raster()' - 10/15/91 - rs
Type 1 local variables
*/
		LPUB8 pvert;          /* ptr to vertex codes in work buff */ 
		LPSL32 pxy;             /* ptr to XY coords in work buff */  /* 6-29-92 */

		LPSW16 p_vloop;        /* ptr to start of loop in expand buff */ 
		LPSL32 p_xyloop;       /* ptr to start of loop in expand buff */ 

		LPUB8 t1bufptr;   /* start of this 'POST' resource */
		LPUB8 t1bufend;   /* end of this 'POST' resource */

#if NON_Z_WIND
		SL32   yprev;               /* 10-07-93 jfd */
#endif

		INTR    p0x; /* p0x, p0y = current locs while rasterizing */
		INTR	p0y;

		SL32   savec;
		SL32   unputflag;
		SL32   decoding;  /* until we see currentfile eexec */
		UW16   code_decrypt;

#if NON_Z_WIND
		SW16   y_goes_up;           /* 10-07-93 jfd */
#endif
        SL32  movetocalled;      /* detect bad chars with path not starting with a moveto() */
		UW16 code;
		SW16 num_vects;          /* # of vectors in contour */
		SW16 num_bez;                /* # of bezier curves in contour */

		UB8  buf[128];
		UB8  token[1025];

} PSI_WORKING;
enum hexasc { T2ASCII, T1HEXFONT, CFF };

typedef struct {

#if USE_PS_ARRAY
/* only enable for non-UFST (ie PostScript encoding) - 5/22/92 - rs */
   MEM_HANDLE  hencodearray; /* sometimes needed with T1 */
#endif

#if CFF_RDR
   MEM_HANDLE  hcffencodearray; /* always needed with CFF, not for T1 */
   MEM_HANDLE  hcharsetarray; /* always needed with CFF, not for T1 */
   cft   cft;   /* so we don't have to BUFalloc() it */

	/* new fields used for processing of CID-in-CFF fonts*/
   SW16 FDidx_cur;		/* current FontDICT that's been read in for CID CFF font */
   MEM_HANDLE	hFDSelect;	/* holds FDSelect data for current CID CFF font */

#endif	/* CFF_RDR */

#if   PST1_RDR
   /* The following fields were added for Type 1 CIDs        */  
   BOOLEAN isCID; /* Type 2 CID-in-CFF uses this too, is set when ROS opcode (12:30) is encountered in a CFF font*/
   SL32 cidCount;
   UL32 cidMapOffset;
   UL32 subrMapOffset;
   UL32 subrMapSdOffset;
   SL32 fdArrayIndex;
   SL32 fdArrayCount;
   SL32 fdBytes;
   SL32 gdBytes;
   SL32 subrBytes;
   SL32 lengthGdValue;
   LPUB8 dataLocLP;
   UL32 dataLocUL;
   LPUB8 privateDictStartLP;
   SL32 privateDictStartUL;
   /* End Type 1 CID fields */
#endif

   MEM_HANDLE  hcharstrings;
   MEM_HANDLE  hsubrs;
   SW16 CharstringType;		/* std type 1 or cff type 2 */
   SW16 defaultWidthX;
   SW16 normalWidthX;
   SW16 stemhints;
   SW16 nsubs;       /* number of subs */
   SW16 n_chars;     /* number of chars in font */
   SW16 curr_char;   /* next char to read from font */
   SW16 StdHW;
   SW16 StdVW;
   SW16 lenIV;
   SW16 inStdHW;
   SW16 inStdVW;
   SW16 numStemSnapH;
   SW16 numStemSnapV;
   SW16 StemSnapH[MAXSTEMS];     /* add 3/2/93 - rs */
   SW16 StemSnapV[MAXSTEMS];     /* add 3/2/93 - rs */
   
   SB8  tfName[FNT_MET_NAM_LEN];    /* jwd, 12/7/05 */
   SB8  famName[FNT_MET_NAM_LEN];   /* jwd, 12/7/05 */

   /* re-do scaled stems */
   SW16 num_x_stems;
   SW16 num_y_stems;
   SL32 scaled_x_stems[MAXSTEMS];
   SL32 scaled_y_stems[MAXSTEMS];

   /* re-do blue value processing */
   SW16 num_blues;
#define MAXBLUES 12
   SW16 round_blues[MAXBLUES];
   SW16 square_blues[MAXBLUES];
   SL32 scaled_round_blues[MAXBLUES];
   SL32 scaled_square_blues[MAXBLUES];
#undef MAXBLUES

   /* re-do hint processing */
#define MAXHINTS 96	/* as per CFF spec */

#if CFF_RDR   /* these hold the raw-unprocessed CFF hints */
   SL32 cff_v1[MAXHINTS];
   SL32 cff_v2[MAXHINTS];
   SW16 cff_type[MAXHINTS];
   SW16 cff_num_hints;
   SW16 cff_hintmask;
#endif

   /* these are the active, processed hints 
   * from a CFF or from a regular TYPE1 font */
   SW16 num_x_hints;
   SW16 num_y_hints;
   SW16 is_sorted;
   SL32 x_pixels[MAXHINTS];
   SL32 y_pixels[MAXHINTS];
   SL32 x_frus[MAXHINTS];
   SL32 y_frus[MAXHINTS];
   SL32 x_scales[MAXHINTS];
   SL32 y_scales[MAXHINTS];
   SL32 t1xtrans;
   SL32 t1ytrans;

/* the following 3 values are used in NEW_PS_HINTS, if T1MMASTER is set */
   SW16 numbblues;
   SW16 numtblues;
   SW16 bot_blues[BOTBLUES];
/* the following value is used in NEW_PS_HINTS, if T1MMASTER is not set */
   SW16 top_blues[TOPBLUES];
   
   SL32 Transform[4];
   SL32 iBlueScale;
   SW16 useblues;               /* !0 -> use BlueScale and BlueShift */
   SW16 iBlueShift;
   SW16 BlueFuzz;                /* 4-May-93 upgrade - rs */
   SW16 trans;
   SW16 overshoot;               /* suppress overshoots < this amount */
   SW16 stem_min;                /* 4-May-93 upgrade - rs */
   SW16 stem_adjust;             /* 4-May-93 upgrade - rs */
   SW16 stem_match;              /* 4-May-93 upgrade - rs */
   SW16 hint_gravity;            /* 4-May-93 upgrade - rs */
   SW16 FontBBox[4];             /* 28-Sep-93 jfd         */
   SL32 ForceBoldThreshold;      /* 29-Jul-98 dlk         */
   SW16 ForceBold;               /* 29-Jul-98 dlk         */

#if (T1MMASTER )
   SW16 inputDV[MAXAXES];        /* UDV from FONTCONTEXT- can be different
                                    from UserDV[] below if contains out of
                                    range values. awr 21-Jul-98 */
   SW16 MMnaxes;                 /* 6/6/93 - rs */
   SW16 Weightvals;              /* 4-May-93 upgrade - rs */
   SL32 weights[MAXWEIGHTS];     /* excess 16 fixed - 4-May-93 upgrade - rs */
   SL32 weights_default[MAXWEIGHTS];
   SL32 NormDV[MAXAXES];		 /* Normalized design vector - CFF - 6/98 dek	*/
   SL32 UserDV[MAXAXES];		 /* User design vector - CFF - 6/98 dek			*/
   SW16 MMnstdvw;                /* 6/6/93 - rs */
   SW16 MMstdvw[MAXAXES];        /* 6/6/93 - rs */
   SW16 MMnstdhw;                /* 6/6/93 - rs */
   SW16 MMstdhw[MAXAXES];        /* 6/6/93 - rs */
   SW16 MMnblendmap;             /* 6/6/93 - rs */
   MEM_HANDLE hMMblendmap;       /* 6/6/93 - rs */
   SW16 MMnblues;                /* 6/6/93 - rs */
   MEM_HANDLE hMMblues;          /* 6/6/93 - rs */
   SW16 MMnotherblues;           /* 6/6/93 - rs */
   MEM_HANDLE hMMotherblues;     /* 6/6/93 - rs */
   SW16 MMnbluescales;           /* 7/29/98 - dlk */
   MEM_HANDLE hMMbluescales;     /* 7/29/98 - dlk */
   SW16 MMnblueshifts;           /* 7/29/98 - dlk */
   MEM_HANDLE hMMblueshifts;     /* 7/29/98 - dlk */
   SW16 MMnstemsnapsH;           /* 7/29/98 - dlk */
   MEM_HANDLE hMMstemsnapsH;     /* 7/29/98 - dlk */
   SW16 MMnstemsnapsV;           /* 7/29/98 - dlk */
   MEM_HANDLE hMMstemsnapsV;     /* 7/29/98 - dlk */
   SW16 MMnfontbbox;             /* 7/29/98 - dlk */
   MEM_HANDLE hMMfontbbox;       /* 7/29/98 - dlk */
   SW16 MMnblendpos;             /* 6/24/98 - dlk */
   MEM_HANDLE hMMblendpos;       /* 6/24/98 - dlk */
   SW16 MMnforcebold;            /* 7/29/98 - dlk */
   MEM_HANDLE hMMforcebold;      /* 7/29/98 - dlk */
#endif /* T1MMASTER */
   FPNUM fontmatrix[6];          /* from original font */
#ifdef LINT_ARGS
   VOID (*transptr) (FSPvoid SL32, SL32, LPSL32, LPSL32);
#else
   VOID (*transptr) ();
#endif /* LINT_ARGS */
/*
Info stored by 'psset_char()'
NOTE: Memory WARANTEED NOT TO MOVE BETWEEN 'set_char()' & 'make_gaso...()'!
PCDATASTR not defined up to here, use VOID for now & fix later
*/
   VOID  *pthis_charstring;/* PCDATASTR - ptr to this charstring */
   SL32   last_seek;/* last position seeked to */
   UW16 last_key; /* last decryption key UL32 to UW16 3/13/92 rs*/
   enum hexasc hexascii; /* formerly (0 - binary, 1 - hex ascii) - now an enum value */
	PSI_WORKING psiw;
	BOOLEAN isFixedPitch;
} PS_BUCK;
typedef PS_BUCK FARPTR *PPS_BUCK;
 
/*---------------------------*/
/*     DIMENSION             */
/*---------------------------*/

typedef struct
{
    UB8   num_dim;       /*  number of dimensions              */
    UB8   stan_STAN_i;   /*  index to STANDARD standard dim    */
    LPUW16  value;         /*  arr of dimension values           */
    LPUB8  attrib;        /*  arr of dim flags (RTZ = bit 0)    */

}  DIMENSION;
typedef DIMENSION FARPTR * PDIMENSION;

/*---------------------------*/
/*   yclass_def_type         */
/*---------------------------*/

typedef struct
{
    UB8   num_yclass_def; /*  num of loc Y class definitions    */
    LPUB8  yline_high_i;   /*  arr of loc Y line indices         */
    LPUB8  yline_low_i;    /*  arr of loc Y line indices         */

}  YCLASS_DEF;
typedef YCLASS_DEF FARPTR * PYCLASS_DEF;

typedef struct {
  /*  character directory  (face header segment) */

    LPSB8          pface_header_seg;
    MEM_HANDLE     hface_header_seg;
    UW16          ch_count;             /* Number of characters */

  /*  FACE GLOBAL SEGMENT.  Subsegments are broken out by offsets
   *  from start of face global segment and size in bytes
   */

    LPSB8      pfgseg;           /* Pointer to face global segment */
    MEM_HANDLE hfgseg;           /* Handle of    "     "      "    */
    UW16      sfgseg;             
    UW16      ogif;              /* Global Intellifont segment */
    UW16      gifct;
    UW16      otrack_kern_seg;   /* track kerning              */
    UW16      strack_kern_seg;
    UW16      otext_kern_seg;    /* text kerning               */
    UW16      stext_kern_seg;
    UW16      odesign_kern_seg;  /* designer kerning           */
    UW16      sdesign_kern_seg;
    UW16      owidth_seg;        /* character width segment    */
    UW16      swidth_seg;
    UW16      oattribute;        /* Attribute header           */
    UW16      sattribute;
    UW16      orast;             /* Raster parameter           */
    UW16      srast;
    UW16      otf_header_seg;    /* Typeface Header segment    */
    UW16      stf_header_seg;
    UW16      ocompound_seg;     /* Compound character         */
    UW16      scompound_seg;
    UW16      odisplay;          /* Display header             */
    UW16      sdisplay;
    UW16      ofont_alias_seg;   /* Font Alias segment         */
    UW16      sfont_alias_seg;
    UW16      ocopy;             /* Copyright notice           */
    UW16      scopy;

#if ROM
    LPUB8      plibfont;         /* "pointer" to lib in font block */
#endif

    UW16    stan_dim_lim;   /*  pix size above which standrd used */
    UW16    stan_STAN_dim_lim; /* size above which STAN stan used */
/*-----------------------------------*/
/*  Global Intellifont segment (100) */
/*-----------------------------------*/

/*      I5.0            Face Identifier                               */
        UW16    if_flag;        /*  flag to indicate if intellifont   */
                                /*  data is present                   */  

/*      I5.1            Global Y Class Definition Data                */
        UW16    num_gylines;     /*  number of Y lines                 */
        LPUW16  gylines;         /*  single arr of Y class Y lines     */
        YCLASS_DEF glob_yclass;
                                /*  actual Y class definitions        */

/*      I5.2            Global Dimension Data                         */
        DIMENSION   glob_x_dim;
        DIMENSION   glob_y_dim;

/*      I5.3            Global Italic Angle Data                      */
        SW16    glob_ital_ang;

/*      I5.4.2          [Generic Screen] Face Substituion Data        */
        UW16    subst_cutin;    /*  width fac. to adjust subst face   */
		    SW16    loc_ital_ang;
    UW16    italic_flag;
	    SW16 gaso_pn;

} IF_BUCK;

#define TT_PLUGPLID  3           /* platformID default for TT plugin   */
#define TT_PLUGSPID  1           /* specificID default for TT plugin   */
                                 /*   Microsoft Unicode, UGL           */
/* moved from ttload_font */
#define  FONT_TYPE_DISK      1
#define  FONT_TYPE_EXTERN    2
#define  FONT_TYPE_ROM       3
#define  FONT_TYPE_DISK_ACT  4          /* to be implemented */
#define  FONT_TYPE_ROM_ACT   5

#if EMBEDDED_BITMAPS && TT_RDR && CGBITMAP
typedef struct {
	SB8		ascender;
	SB8		descender;
	UB8		widthMax;
	SB8		caretSlopeNumerator;
	SB8		caretSlopeDenominator;
	SB8		caretOffset;
	SB8		minOriginSB;
	SB8		minAdvanceSB;
	SB8		maxBeforeBL;
	SB8		minAfterBL;
	SB8		pad1;
	SB8		pad2;
} SBITLINEMETRICS;

typedef struct {
	UW16	glyphCode;
	SB8		xOffset;
	SB8		yOffset;
} EBDTCOMPONENT;
typedef EBDTCOMPONENT FARPTR * PEBDTCOMPONENT;
typedef EBDTCOMPONENT FARPTR * FARPTR * PPEBDTCOMPONENT;

typedef struct {
	UW16	firstGlyphIndex;
	UW16	lastGlyphIndex;
	UL32	additionalOffsetToIndexSubtable;
} INDEXSUBTABLEARRAY;
typedef INDEXSUBTABLEARRAY FARPTR * PINDEXSUBTABLEARRAY;
typedef INDEXSUBTABLEARRAY FARPTR * FARPTR * PPINDEXSUBTABLEARRAY;

typedef struct {
	UB8	height;
	UB8	width;
	SB8	horiBearingX;
	SB8	horiBearingY;
	UB8	horiAdvance;
	SB8	vertBearingX;
	SB8	vertBearingY;
	UB8	vertAdvance;
} BIGGLYPHMETRICS;
typedef BIGGLYPHMETRICS FARPTR * PBIGGLYPHMETRICS;

typedef struct {
	UB8	height;
	UB8	width;
	SB8	BearingX;
	SB8	BearingY;
	UB8	Advance;
} SMALLGLYPHMETRICS;
typedef SMALLGLYPHMETRICS FARPTR * PSMALLGLYPHMETRICS;

typedef struct {
	UW16	glyphCode;
	UW16	offset;
} CODEOFFSETPAIR;
typedef CODEOFFSETPAIR FARPTR * PCODEOFFSETPAIR;

typedef struct {
	UW16	indexFormat;
	UW16	imageFormat;
	UL32	imageDataOffset;

	union {
		struct {
			UL32			offsetArray[1];
		} INDEXSUBTABLE1;

		struct {
			UL32			imageSize;
			BIGGLYPHMETRICS	bigMetrics;
		} INDEXSUBTABLE2;

		struct {
			UW16			offsetArray[1];
		} INDEXSUBTABLE3;

		struct {
			UL32			numGlyphs;
			CODEOFFSETPAIR	glyphArray[1];
		} INDEXSUBTABLE4;

		struct {
			UL32			imageSize;
			BIGGLYPHMETRICS	bigMetrics;
			UL32			numGlyphs;
			UW16			glyphCodeArray[1];
		} INDEXSUBTABLE5;
	} u;
} INDEXSUBTABLE;
typedef INDEXSUBTABLE FARPTR * PINDEXSUBTABLE;
typedef INDEXSUBTABLE FARPTR * FARPTR * PPINDEXSUBTABLE;

typedef struct {
	UW16				indexFormat;
	UW16				imageFormat;
	UL32				imageDataOffset;
} INDEXSUBHEADER;
typedef INDEXSUBHEADER FARPTR * PINDEXSUBHEADER;

#if 1	/* 01-15-04 jfd (from SWP) */
/* NOTE: we use the BOLD and OBLIQUE extensions in both STIK and non-STIK fonts, where we */
/* may need embedded bitmaps/graymaps for Normal, Bold, Oblique, and Bold-Oblique: they use */
/* currently 'undefined' bits of the <flags> field of the BITMAPSIZETABLE */
/* NOTE: we are using bitDepth > 1 for embedded graymaps. Specifically */
/* bitDepth==4, is being used for 4 bit per pixel grayscale images */
#define EMB_BITMAP_ITEM   1
#endif	/* 01-15-04 jfd */

typedef struct {
	UL32				indexSubTableArrayOffset;
	UL32				indexTableSize;
	UL32				numberOfIndexSubTables;
	UL32				colorRef;
	SBITLINEMETRICS		hori;
	SBITLINEMETRICS		vert;
	UW16				startGlyphIndex;
	UW16				endGlyphIndex;
	UB8					ppemX;
	UB8					ppemY;
	UB8					bitDepth;
	SB8					flags;
} BITMAPSIZETABLE;
typedef BITMAPSIZETABLE FARPTR * PBITMAPSIZETABLE;
typedef BITMAPSIZETABLE FARPTR * FARPTR * PPBITMAPSIZETABLE;

typedef struct {
	LPUB8				EBLCTable;		/* ptr to EBLC Table						*/
	UL32				EBLCSize;		/* size of EBLC table						*/
										/*   0= table NOT in font					*/
	UL32				EBLCOffset;		/* offset to EBLC Table (10-05-00)			*/
	LPUB8				EBDTTable;		/* ptr to EBDT Table						*/
	UL32				EBDTSize;		/* size of EBDT table						*/
										/*   0 = table NOT in font					*/
	UL32				EBDTOffset;		/* offset to EBDT Table (10-05-00)			*/
	UL32				numbitmapSizeTables;
										/* number of bitmapSizeTable tables in font	*/
	PBITMAPSIZETABLE	pbitmapSizeTable;
										/* pointer to bitmapSizeTable elemenet		*/
										/* to which requested glyph belongs			*/
	MEM_HANDLE			bst_hndl;		/* handle to bitmapSizeTable element above	*/
	PINDEXSUBTABLEARRAY	pindexSubTableArray;
										/* pointer to indexSubTableArray element	*/
										/* to which requested glyph belongs			*/
	MEM_HANDLE			ista_hndl;		/* handle to indexSubTableArray element above */
	PINDEXSUBTABLE		pindexSubTable;
										/* pointer to indexSubTable element to which*/
										/* requested glyph belongs					*/
	MEM_HANDLE			ist_hndl;		/* handle to indexSubTable element above	*/
} EMBEDDED_BITMAP_FONT;
typedef EMBEDDED_BITMAP_FONT FARPTR * PEMBEDDED_BITMAP_FONT;

typedef struct {
	LPUB8		embedded_bitmap_data;
								/* pointer to bitmap data for requested char*/
								/* (format based on 'imageFormat' value in	*/
								/* indexSubTable struct						*/
	MEM_HANDLE	emb_bmp_hndl;	/* handle to bitmap data for req char above	*/
	UL32        metrics_size;	/* size of metrics data */
	UL32        offset;			/* offset to embedded item */
	UL32        size;			/* size of embedded item in bytes */
} EMBEDDED_BITMAP;
typedef EMBEDDED_BITMAP FARPTR * PEMBEDDED_BITMAP;
#endif	/* EMBEDDED_BITMAPS && TT_RDR && CGBITMAP */

/* for CCC font */
#if CCC
typedef struct {
	SL32 numBits_LOCA;  /* number of bits used for LOCA */
	/* for v7 CCC, qwu 08-20-04 */
	/* SW16 minVal_LSB, numBits_HMTX;*/	/* mimium value for LSB and number of bits for HMTX */
	SW16 minVal_LSB, numBits_LSB;	/* mimium value and number of bits for LSB */
	SW16 minVal_AW, numBits_AW;	/* mimium value and number of bits for AW */
	SW16 minVal_TSB, numBits_TSB;	/* mimium value and number of bits for TSB */
	SW16 minVal_AH, numBits_AH;	/* mimium value and number of bits for AH */
	
	UL32 numBits_GLYF;				/* number of bits for GLYF */
	/* for v7 CCC, qwu 08-20-04 */
	BOOLEAN cccVersion7;
} CCC_INFO;
#endif

typedef struct {
    MEM_HANDLE buffInfo;         /* fs_GlyphInfoType structure         */
    MEM_HANDLE buffInput;        /* fs_GlyphInputType structure        */
    MEM_HANDLE trMatrix;         /* 3x3 transformation matrix          */
    MEM_HANDLE GlobalTTHndl;     /* memory handle for global TT data   */
#if TT_PCLEOI
    MEM_HANDLE GalleySegHndl;    /* memory handle to Galley Segment    */
                                 /*   NIL_MH= Segment NOT in font      */
    UL32 GalleySegSize;          /* size of Galley Segment             */
                                 /*   0= Segment NOT in font           */
	UL32 VertRotSegSize;         /* size of Vert Rotation Seg. AJ   8-12-03 */
	MEM_HANDLE VertRotSegHndl;   /* memory handle to Vertical Rotate AJ   */
	MEM_HANDLE VertExSegHndl;       /* memory handle to Vertical Exclude Segment */
	UL32 VertExSegSize;             /* size of VE Segment*/
#endif /* TT_PCLEOI */
#if (ASIAN_ENCODING && (TT_DISK || TT_ROM || TT_ROM_ACT)) /*GSUB*/
    MEM_HANDLE GSUBTableHndl;    /* memory handle for DISK font GSUB Table */
                                 /*   DISK File:  NIL_MH=table NOT in font */
                                 /*   ROM File:   Always NIL_MH            */
    UL32       GSUBSize;         /* size of GSUB table                     */
                                 /*   0= table NOT in font                 */       
    UW16       GSUBVrtGCount;    /* count of 'GSUB' glyph substitution     */
                                 /*   pairs                                */
    LPUB8      pGSUBHorGIndxs;   /* pointer to start of horizontal         */
                                 /*   'GSUB' glyph indexes                 */
    LPUB8      pGSUBVrtGIndxs;   /* pointer to start of vertical sub.      */
                                 /*   'GSUB' glyph indexes                 */
	UW16       GSUBCoverageFormat; /* format of the coverage table            */ 
	UW16       GSUBHorRangeCount;  /* # of ranges if fmt of coverage tbl is 2 */
#endif /* (ASIAN_ENCODING && (TT_DISK || TT_ROM || TT_ROM_ACT))  */
#if EMBEDDED_BITMAPS && TT_RDR && CGBITMAP
	/* EBLC */
	EMBEDDED_BITMAP_FONT	embf;
	EMBEDDED_BITMAP			emb;
	BOOLEAN					BitmapTablesEmbedded;	/* Embedded bitmap tables exist in font */
#endif	/* EMBEDDED_BITMAPS && TT_RDR && CGBITMAP */
#if  (TT_ROM || TT_ROM_ACT)
    LPUB8      ROMHndl;          /* pointer to font data in ROM        */
#endif
#if TT_PCLEOI
    LPUB8      baseGlyphPtr;     /* PCLEO: saves ptr to glyph data     */
    UW16       baseGlyphID;      /* Distinguishes comp char ID from components */
    UW16       baseGlyphLen;     /* Size of glyph data                 */
	/* keb 8/99 */
	UW16       baseGlyphAW;
    MEM_HANDLE VrtSubTTHndl;     /* memory handle, Vert Subs. table    */
    UL32       VrtSubTTSize;     /* size of Vertical Substitution tbl. */
#endif
    UL32       glyphTableOffset; /* offset to glyph table in disk/ROM font */
    UL32       glyphTableSize;   /* size of glyph table in disk/ROM font */
    UL32       GlobalTTSize;     /* size of global font data           */
    UW16       platId;           /* platform id (for character mapping)*/
    UW16       specId;           /* specific id   "      "        "    */
    UW16       langId;           /* language id (to distinguish GB from BIG5)*/
    CGFIXED    spotsize;
	UL32	   fontStartOffset;	 /* offset to fontstart - 0 unless TTC */
	UL32	   font_type_TT;	 /* used to be local font_type in ttload_font */			
SW16	   ttc_index;		 /* index of font within TTC (TrueTypeCollection) file. */
#if	0	/* 02-27-06  BUCKET-related changes */
	BOOLEAN	   useCmapTable;	 /* whether to use CMAP table to get glyph
									index - if FALSE, set glyphIndex directly */
#endif	/* 02-27-06  BUCKET-related changes */
    SW16       FontBBox[4];      /* xmin,ymin,xmax,ymax                */
    UW16       emResolution;     /* font resolution in design units    */
	/* keb 7/03 */
	BOOLEAN    verticalMetricsExist;  /* indicated the presence of the vhea table */
#if TT_TTPLUG
    FONTCONTEXT  TTfc;           /* FC for each TrueType font          */
#endif
#if 0 /* 02-27-06 BUCKET-related changes */
	UL32       CMAP_offset;      /* offset to CMAP table from start of font */
#endif /* 02-27-06 BUCKET-related changes */
#if TT_RDR && CFF_RDR            /* 05-23-01 jfd */
	UL32       CFFTableOffset;   /* offset to OTCFF table in disk/ROM font */
	UL32       CFFTableSize;     /* size of OTCFF table in disk/ROM font */
#endif	/* TT_RDR && CFF_RDR */

/* for CCC font */
#if CCC
	CCC_INFO		ccc_info;
#endif

#if DIMM_DISK
	UL32		locaTableOffset;	/* ROM address */
	UL32		locaTableSize;
	UL32		hmtxTableOffset;	/* ROM address */
	UL32		hmtxTableSize;
    UL32		vmtxTableOffset;  
#endif /* DIMM_DISK */
} TT_BUCK;

#define NOFONT 0                 /* definitions applicable to         */
#define FONT_LOADED 1            /* 'FontState' in FCO_BUCK structure */

#if FCO_RDR
typedef struct {
    MEM_HANDLE fcoFont;          /* Handle to FONTTYPE object */
    MEM_HANDLE fcoChar;          /* Handle to CHARTYPE object */
    MEM_HANDLE fcoTrans;         /* Handle to PIXEL_DATA object (x and y) */
    SL32       tMatrix[4];       /* Transformation matrix     */
    UW16       matrixType;       /* identity, orthogonal, or arbitrary */
    UW16       design_EmRes;     /* Design unit resolution per em */
    UW16       spaceBand;        /* Width of spaceband in design units */
    UW16       unicode;          /* Unicode of current character */
    UW16       orThreshold;      /* orThreshold from TTFONTINFOTYPE  */
   FONTCONTEXT fcoFC;            /* Save fontcontext of each bucket. */
    SB8        compositionFlag;  /* 0=IF compatible; 1=TT compatible */
    UB8        FontState;        /* Set to FONT_LOADED after FCfontNew() has executed successfully */
    UW16       ThinSpace;        /* Width of Thin space in design units */
    UW16       ENSpace;          /* Width of EN space in design units */
    UW16       EMSpace;          /* Width of EM space in design units */
#if FCO_CONVERGENTFONT
    SL32       oblMatrix[4];     /* Obliqued matrix for Convergent Fonts */
    SB8        cvfChangeScale;   /* if converged font uses different scale factor */
    UW16       cvfScale1;        /* scale factor of base font */
    UW16       cvfScale2;        /* scale factor of alternate font */
#endif  /* FCO_CONVERGENTFONT */
#if BOLD_FCO
    SW16       xbold;
    SW16       ybold;
#endif
}  FCO_BUCK;
#endif


/*--------------------------------*/
/*     BUCKET                     */
/*--------------------------------*/

#if UFST_MULTITHREAD
#define MAX_THREADS 16
#endif
typedef struct
{
    DLL       link;
    HBUCKET   my_handle;      /* memory handle of this structure */
    BOOLEAN   locked;         /* BUFalloc() can't free if locked */
    SW16      bucket_num;     /* See INDEX_ENTRY.bucket_num. Used
                               * by IF_RDR, TT_IFPLUG, PS_IFPLUG */
    SL32      tfnum;          /* Typeface number                 */
    SL32       fh;             /* File handle, -1 if no open file */
#if DIMM_DISK
    SL32    curpos;             /* Current position in DIMM "file" */
#endif
 
#if 0	/* 02-27-06  BUCKET-related changes */
	UW16      cur_ssnum;      /* current symbol set number       */
#endif	/* 02-27-06  BUCKET-related changes */
#if 0 /* 02-27-06 BUCKET-related changes */
							  UW16      usePlugins;     /* 1 = character set Augmentation with buckets;

                              * 0 = no plugins                  */
#endif  /* 02-27-06 BUCKET-related changes */
    UW16      fc_format;      /* format word in fontcontext structure */
    UL32      fc_ExtndFlags;  /* ExtndFlags word in fontcontext structure */
    SL32       font_type;      /* FONTCONTEXT format field bits 10-13  */
    SL32       extern_font;    /* FC_EXTERN_TYPE | EF_FORMAT16_TYPE bit */
    UW16      fst_type;       /* FC_IF_TYPE    0x0000  Intellifont
                               * FC_PST1_TYPE  0x1000  PostScript Type 1
                               * FC_TT_TYPE    0x2000  TrueType
                               * FC_FCO_TYPE   0x3000  MicroType */
    LPUB8     cur_font_hdr;   /* current pcleo header              */
    BOOLEAN   has_path;       /* Use pathname to identify the font */
#if DISK_FONTS
    PATHNAME  pathname;
#endif
#if DYNAMIC_FONTS && IF_RDR
    BOOLEAN		dynIFActive;    /* Dynamic font (Intellifont)      */
#endif

#if CFF_RDR	&& TT_RDR	/* 05-23-01 jfd */
	VOID      *potcff;        /* ptr to OpenType CFF bucket (if applicable) */
	BOOLEAN    otcff_font;    /* indicates whether font is OpenType CFF font */
#endif	/* CFF_RDR */

#if UFST_MULTITHREAD
	UL32 thread_ids[MAX_THREADS];
#endif

#if TT_PCLEOI
	FILECHAR *    efm_font_name;	/* name of "EXTERNAL FONT MODULE" font */
#endif
    VOID     *pft;            /* ptr to struct of functions to handle font */

    union {                   /* FST specific */
#if PST1_RDR
        PS_BUCK  ps;
#endif
#if IF_RDR
        IF_BUCK  in;
#endif
#if TT_RDR
        TT_BUCK  tt;
#endif
#if FCO_RDR
        FCO_BUCK fco;
#endif
    } p;
} BUCKET;
typedef BUCKET FARPTR * PBUCKET;
typedef BUCKET FARPTR * FARPTR * PPBUCKET;



/*------------------------------*/
/*   Per Character Definition   */
/*------------------------------*/

typedef struct
{
     SW16VECTOR origin;
     SW16VECTOR escape;

     SW16    num_parts;      /* number of parts in the character    */
     BOOLEAN is_compound;    /*  Flag this character as compound    */
#if SLIM_FONTS
     UB8   mirror_flags;   /* flags from num_parts to describe mirroring */
#endif /* SLIM_FONTS */
}  CHR_DEF_HDR;
typedef CHR_DEF_HDR FARPTR * PCHR_DEF_HDR;



/*------------------------------*/
/*  Character (Part) Definition */
/*------------------------------*/

typedef struct
{
    UW16 cgnum;
    SL32  buck_search_lvl;       /* bucket level where this part "lives" */
    SW16  index;
    SW16VECTOR   offset;
    SW16VECTOR   bmorigin;
    BOX          pix_bound;
#if PCLEO_RDR
    LPUB8       pgif;           /*  ptr to global IF data in PCLEO hdr */
#endif
} CHR_DEF;
typedef CHR_DEF FARPTR * PCHR_DEF;


typedef struct
{
    SW16   format;      /* 1, 2, 3 for linear, quadra, cubic             */
    SW16   nloop;       /* Number of contour loops in character          */
    UW16   est_pnts;    /* estimated number of vector points             */
    UW16   est_segs;    /* estimated number of segments                  */
    SL32   est_size;    /* Estimated outline size including OLHEADERISIZ */

    SW16   xscale;  /* Did not conditionally compile on !VLCOUTPUT becaues */
    SW16   yscale;  /* these values appear in so many modules. The extra */
                    /* space they take up is not significant */
#if VLCOUTPUT
    SW16   VLCpower;  /* power of two to scale up in output processors */
#endif
    SW16   escapement;
    INTRBOX olbox;
    SL32VECTOR origin_cs;  /* char origin in fops; changed to SL32VECTOR, mby 12-11-94 */

    COUNTER  tran_run_ct;   /* number of transition runs = number of
                             * changes in y direction in character outline
                             */

   /* The following fields were added from old CHAR_STATS structure. */

    SL32  xmin, xmax, ymin, ymax;       /* char bbox in fops */
    SL32   ct_lineto;
    SL32   ct_quadto;
    SL32   ct_cubeto;
    SW16VECTOR dorigin;               /* char origin in design units       */
    SL32   dxmin, dxmax, dymin, dymax; /* char bounding box in design units */

   /* The following fields were added to support the new compile option    */
   /* DU_ESCAPEMENT.                                                       */

    SW16 du_emx;               /* x-size of em box in font design units    */
    SW16 du_emy;               /* y-size of em box in font design units    */
    /* keb 4/00 */
#if TT_RDR
	SW16 xl_char_class;
	SW16 yDescender;
#endif
/* #if GET_VERTICAL_METRICS		08-20-04 qwu */  
	UW16 advanceHeight;
	SW16 topSideBearing;      /* JWD, 04-22-03. */
/* #endif		08-20-04 qwu */ 
#if TT_SCREENRES
	SW16	pixelWidth;   /* "devAdvWidth", shifted to 1/16th pixel    */
                              /* however, this should be INTEGRAL # pixels */
    CGPOINTFX advanceWidth;   /* Scaled escapement, 16.16. jwd, 08/18/02.  */    
#endif

} CHAR_STATS;
typedef CHAR_STATS FARPTR * PCHAR_STATS;

typedef struct {
    SL32  BBox[4];
    SL32  emRes;
} BBoxRes;

#endif	/* __IX__ */

