/* $HopeName: GGEufst5!rts:inc:cgif.h(EBDSDK_P.1) $ */

/* Copyright (C) 2004 Agfa Monotype Corporation. All rights reserved. */

/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:inc:cgif.h,v 1.3.8.1.1.1 2013/12/19 11:24:05 rogerb Exp $ */
/* cgif.h */


/*
 *     21-Jul-90   awr   moved segment definitions for CGIFsegments()
 *                       from segments.h to cgif.h
 *     23-Jul-90   awr   added Blake's typePath to IF_CONFIG
 *     26-Jun-90   bjg   Added IFOUTLINE OUTLINE_LOOP and OUTLINE_CHAR.  Also
 *                       added error flags for outline or bitmap not installed.
 *     17-Aug-90   awr   corrected byte count comment for FONTCONTEXT
 *      7 Jan 91   jd,ss Added FONTCONTEXT.font_hdr field for PCLEO support
 *                       Added MASK defines and macros for FONTCONTEXT.format
 *                        bit usage
 *     13-Jan-91   awr   Added ERRhix_extra error code.
 *     17-Jan-91   awr   Added ERRinvalid_seg_key
 *     23-Jan-91   awr   Compiling Font Context masks and macros unconditionally.
 *     28-Jan-91   jfd   Declared "...cc_buf_size" conditionally based
 *                       on (MAX_BM_BITS > 16)
 *                       Declared xorigin and yorigin from structure IFBITMAP
 *                       conditionally based oon (MAX_BM_BITS > 16).
 *                       ADDed MASK defines for FONTCONTEXT.format bit usage
 *                       for point size units designation.
 *     29-Jan-91   jfd   Added description for bits 8-9 of format field.
 *     20-Dec-90   dET   Add support for multi-model MSC compilation.
 *      3-Feb-90   awr   added typedefs for HIFOUTLINE & PMEM_HANDLE.
 *      8-Feb-91   awr   added typedef for PHIFOUTLINE.
 *     17-Feb-91   awr   moved MEM_HANDLE typedefs to ifmem.h
 *     06-Mar-91   jfd   Conditionally compiling fields xscale and yscale
 *                       in structure IFOUTLINE so that IFOUTLINE remains
 *                       aligned with IFBITMAP.
 *                       Changed value of HEADERSIZE from "34" to
 *                       "sizeof(IFBITMAP)-4".
 *      28-Apr-91  awr   Matrix varients of FONTCONTEXT. Output resolution
 *                       option pixels/inch
 *     05-Jun-91   jfd   Added function declaration for CGIFcache_purge().
 *                       (FIX FROM EARLIER VERSION)
 *                       Macro FC_ROM_TYPE was missing.
 *     10-Jun-91  jfd   Added declaration for module CGIFps().
 *     19-Jun-91  jfd   Changed arguments for CGIFps() calling sequence.
 *     30-Jun-91  awr   Corrected #define FC_INCHES_TYPE
 *     02-Jul-91  jfd   Changed arguments for CGIFps() calling sequence.
 *      4-Jul-91  awr   Chg'd IFBITMAP and IFOUTLINE arrays from 4 to 1 byte
 *     05-Jul-91  rs    Add prototype for CGIFps() - LINT_ARGS && !MULTICALLER
 *                      Moved ERRdu_pix_range from comp_pix.c
 *     07-Aug-91  jfd   Added argument PPSPARAMSTR to CGIFps() function
 *                      prototype.
 *                      Added typedef for PPSPARAMSTR.
 *                      Added typedef for PSPARAMSTR (from ps_type1.h)
 *                      Added #define for MAXPATHLEN (from ps_type1.h)
 *                      Chg'd size of IFBITMAP and IFOUTLINE "bm" arrays from
 *                      1 to 4 bytes (Was affecting computation of HEADERSIZE)
 *                      Added function prototype for CGIFcache_purge() if
 *                      LINT_ARGS & (! MULTICALLER).
 *     09-Sep-91  awr   Replaced "#define HEADERSIZE..." with "define
 *                      BMHEADERSIZE..." and #define OLHEADERSIZE...".
 *                      In structure IFOUTLINE, no longer conditionally
 *                      compiling xscale and yscale based on MAX_BM_BITS.
 *     13-Sep-91  jfd   Changed CGIFbitMap() to CGIFmakechar().
 *     16-Sep-91  jfd   Added error codes ERR_ps_toomanystems and
 *                      ERR_ps_toomanystdwid.
 *     17-Sep-91  jfd   In structure IFBITMAP, changed xorigin and yorigin
 *                      to LONG instead of either LONG or WORD.
 *     30-Sep-91  awr   Added CGIFinitstate bits.
 *                det   In structure IFOUTLINE, changed size from WORD
 *                      to UWORD so as to be able to handle sizes 
 *                      greater than 32K.
 *     30-Sep-91  rs    Add 'FC_PST1_TYPE' to FONTCONTEXT 'format' field for
 *                      PostScript Type 1.
 *     02-Oct-91  rs    Added declaration for module CGIFpsi().
 *     07-Nov-91  jfd   In structure IFOUTLINE, changed size from UWORD to
 *                      LONG so as to be able to handle sizes greater than
 *                      64K.
 *                      In structure OUTLINE_CHAR, changed size from UWORD
 *                      to LONG for same reason. Added WORD field "pad" so
 *                      as to maintain long alignment.
 *     21-Nov-91  jfd   Added error code "ERR_ps_char_not_in_list".
 *     27-Nov-91  rs    Add PostScript Type 1 Input error codes
 *      1-Jan-91  awr   Added tt bit to FONTCONTEXT format field.
 *                      Conditionally compiled font_hdr field in FONTCONTEXT
 *                      if(PCLEO_RDR || PST1I || TT_RDR)
 *      7-Mar-92  awr   Removed FC_DISK_TYPE
 *     13-Mar-92  jfd   Added #define ERR_incompatable_plugin 8 macro
 *     03-Apr-92  rs    Portability cleanup (see port.h).
 *     21-Apr-92  rs    Conditionalize 'MAXPATHLEN' on _WINDOWS.
 *     20-Jun-92  awr   Changed IFOUTLINE bounds to INTRs
 *                      Modified FC_IS_OUTLINE()
 *     22-Jun-92  jfd   Added "#define ERR_ov_16_bit_value 95".
 *      4-Jul-92  awr   Conditionally compiled in IFCONFIG structure elements
 *                      only needed for CHAR_SIZE or CACHE.
 *      6-Jul-92  mby   Added TrueType error codes -- 0x7000 range.
 *     08-Jul-92  rs    Code cleanup.
 *     10-Jul-92  jfd   Added #define  ERR_cubic_not_installed.
 *     10 Jul 92  ss    Changed OUTLINE_CHAR.bm to ol.
 *                      Changed OUTLINE_CHAR.loop[4] to [1] and adjusted
 *                      OLHEADERSIZE accordingly.
 *     21-Jul-92  awr   Changed conditional compiles.
 *     07-Aug-92  rs    Compatibility w/Windows 3.1 (FIXED, POINTFX).
 *      7 Aug 92  ss    Added ERRcompact_pcleo.
 *     12-Aug-92  jfd   Added macro MDES2BM for speed.
 *     14-Aug-92  jfd   Added error code ERR_bold_not_enabled.
 *     16-Aug-92  rs    Make declaration of 'PSPARAMSTR' dependent upon
 *                      'PST1' defined as non-zero (PostScript output).
 *                      Declaration of 'CGIFps()' dependent upon 'PST1'.
 *                      Remove declaration of 'CGIFpsi()'.
 *     04-Sep-92  jfd   Added error code ERR_kern_unsupported (for PS or TT).
 *     08-Sep-92  jfd   Changed TT error codes from hex values (0x7000-0x7012)
 *                      to decimal (400-412) so that they fall within the
 *                      range of other UFST error codes.
 *     03-Sep-92  awr   Added CGIFtt_universal()
 *     12-Oct-92  awr   Added not_intellifont bit to CGIFinitstate
 *     22-Jan-93  jfd   Added fields "du_emx" and "du_emy" to IFBITMAP.
 *                      Added fields "du_emx" and "du_emy" to IFOUTLINE.
 *     28-Jan-93  mby   Added ERRfont_bbox.
 *     01-Feb-93  jfd   Added fields "band_top" and "band_bot" to
 *                      FONTCONTEXT structure.
 *                      Added error code "ERR_invalid_band".
 *     03-Feb-93  mby   Added FC_APPGEN_TYPE and FC_ISAPPGEN() macro.
 *     08-Feb-93  mby   Added CGIFbound_box() prototype [!MULTICALLER].
 *     02-Mar-93  rs    Add error codes ERR_psi_scan_StemSnapH &
 *                      ERR_psi_scan_StemSnapV.
 *     03-Mar-93  mby   Remove PFONTCONTEXT argument from CGIFbound_box() proto.
 *     08-Mar-93  jfd   Removed define for CGIFinitstate bit "not_init" 
 *                      because it is not utilized.
 *     15-Mar-93  rs    Add error codes for PostScript Type 0 input (Asian).
 *     27-Mar-93  rs    More error codes for Asian (Kanji).
 *     21-Apr-93  mby   Error codes (150-51) for dynamic fonts.
 *     05-May-93  mby   Add declarations for CGIFfont_purge();
 *                      make CGIFcache_purge() a macro.
 *                      Remove cond. code around FONTCONTEXT.font_hdr
 *     06-May-93  jfd/mby
 *                      In FONTCONTEXT structure, conditionally compiled
 *                      field "font_hdr" based on (PCLEO_RDR || NON_IF_FONT
 *                      || DYNAMIC_FONTS)
 *     14-May-93  jfd   In IFBITMAP structure, changed "bm" field from
 *                      "UB8 bm[4]" to "UL32 bm[1]" so that it will be
 *                      properlly aligned for 32-bit RASTER_ORG value.
 *     21-May-93  jfd   Reversed comments for band_top and band_bot in
 *                      FONTCONTEXT structure.
 *     06-Jun-93  rs    Add error codes for type 1 multi master.
 *     08-Jun-93  mby   Added prototype for CGIFbbox_IFPCLEOchar() &
 *                      #define CGIFP_IF_SCALABLE
 *     01-Jul-93 maib   Added FC_NON_Z_WIND bit to the fontcontext format
 *                      field, and macro FC_ISNONZWIND to determine if this
 *                      bit is set
 *     05-Jul-93  awr   Added ERRnzw_mem_overflow
 *                      Changed calls for CGIFmakechar() and CGIFtilebitMap()
 *     06-Jul-93  mby   Added p_AltWidTab to FONTCONTEXT.
 *                      #defined FC_ILLEGAL_TYPE
 *     13-Jul-93  mby   Changed function proto for CGIFbbox_IFPCLEOchar() --
 *                      added bad char list. #defined CGIFP_MAX_BADCHARS. 
 *     15-Jul-93  mby   New error codes for disabled CGIF functions.
 *     16-Jul-93  maib  Added ERR_TT_FSCHARBEARINGS error code
 *     23-Jul-93  rs    Add 'ERR_psi_badMMblendmap' error code.
 *     02-Aug-93  maib  Changed prototypes, for MULTICALLER and multi-tasking
 *     30-Aug-93  awr   Added CGIFtile_nzw_buf()
 *     02-Sep-93  jfd   Added typedef for PPIFOUTLINE.
 *     03-Sep-93  jfd   Added missing function prototype for CGIFbmheader()
 *                      if !LINT_ARGS.
 *     13-Sep-93  mby   Added ERR_TT_ENTITY1.
 *     27-Sep-93  jfd   Added error code "ERR_psi_scan_ps_font_bbox".
 *     23-Nov-93  maib  Moved defines CGIFP_IF_SCALABLE and CGIF_MAX_BADCHARS
 *                      outside of prototype declarations
 *     05-Jan-94 jfd    Added error code "ERR_incomp_bitmap_width".
 *     27-Jan-94 jfd    Conditionally compiled out MDES2BM macro based on
 *                      !defined(_AM29K).
 *     12-Apr-94 jfd    Added error code ERR_psi_scan_Fulnam.
 *     03-Jun-94 jfd    Changed all occurrences of ENTRY to CGENTRY to resolve
 *                      conflict on some compilers.
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * FCO Changes from 1.33.1.3:
 *     11-Mar-94 mby    Changed FONTCONTEXT.format to add FCO font type.
 *                      Use bit 5 for future expansion. Added FCO errors.
 *     26-May-94 mby    Declarations for CGIFfco_Open(), CGIFfco_Close(),
 *                      CGIFtt_Generate(), CGIFtt_Free().
 *                      Added new FCO error codes.
 *     29-Jul-94 mby    Added new error 'ERR_fco_FCcompCharPiece'.
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 *     18-Aug-94 jfd    Changed arg 2 of CGIFfco_Open() from 'int *' to
 *                      'LPSW16' for WINDOWS.
 *     22-Aug-94 jfd    Changed args 1 and 2 of CGIFtt_Generate() ('int *'
 *                      --> 'LPSW16', 'LPUB8 *' --> 'LPLPUB8' for WINDOWS.
 *     09-Sep-94 mby    Added new error 'ERR_fco_versionError'.
 *     11-Sep-94 jfd    Added prototype for CGIFhdr_font_purge().
 *     13-Sep-94 mby    Added new error 'ERR_fco_SegmentSharing'
 *     16-Sep-94 mby    Added prototypes for CGIFfco_Plugin().
 *     20-Sep-94 jfd    Added new error code ERRhdr_font_purge.
 *     30-Sep-94 mby    Added new error 'ERR_fco_Init'
 *     20-Oct-94 mby    In prototypes of CGIFfco_Close(), CGIFfco_Plugin(),
 *                      CGIFtt_Free() changed argument from "int" to "SW16"
 *                      for consistency with CGIFfco_Open().
 *     14-Dec-94 rs     Changed arg 2 of CGIFtt_Generate() from LPLPUB8
 *                      to LPHPUB8.
 *     06-Jan-95 SBM    Added new arg to CGIFtt_Generate().
 *     09-Jan-95 rob    Implement support for > 64KB memory for int mem mgr.
 *     27-Jan-95 mby    Added new error code 'ERR_fco_nzwCode'.
 *     24-Mar-95 SBM    Modified CGIFtt_Free to use a Ptr instead of a MEM_HANDLE.
 *     31-Mar-95 jwd    Added prototype for new interface function, CGIFfco_access();
 *                      Added FCO segment keys for use in CGIFfco_Access function.
 *     18-Apr-95 jfd    Added "dl_ssnum" field to FONTCONTEXT.
 *                      Added function prototype for CGIFchIdptr().
 *     01-May-95 mby    Added new error code 'ERR_fco_OutlCode', 513.
 *     23-May-95 mby    Added new error code 'ERR_char_unavailable'
 *     11-Sep-95 awr    Grayscaling changes to FONTCONTCONTEXT and IFBITMAP
 *                      and error return codes
 *   29-Jan-96 mby  Changed "size" in IFBITMAP to SL32.
 *   13-Feb-96 mby  Added FC_TRANSITION_TYPE; FC_ISTRANS macro.
 *   13-Jun-96 jwd  Added new error code 'ERR_TT_PCLEONOVS, 416.
 *   17-Jun-96 dbk  Moved ERR_TT_MEMFAIL to handle FCO as well as TT Only Apps.
 *   07-Aug-96 mby  Added new error code 'ERR_TT_NO_IDS, 417.
 *   26-Aug-96 mby  BIT 6 of fontcontext.format is the PCL 6 emulation flag.
 *                  If set, MicroType TT fonts change their bucket search
 *                  path to look at MT limited sensitive plugins and then the
 *                  MT/IF Universals. MicroType IF fonts return their widths
 *                  in 2048 units, not 8782.
 *   01-Oct-96 jfd  Added 2 new fields to the IFBITMAP ASIAN architecture:
 *                  'f' and 'b' MEM_HANDLES.
 *   16-Oct-96 jfd  In IFBITMAP structure, moved ASIAN 'f' and 'b'
 *                  handles to position after "index" field.
 *                  In IFOUTLINE structure, added ASIAN 'f' and 'b' handles.
 *   23-Oct-96 bjg  Added emboldening for PCL 6 emulation.
 *   24-Oct-96 bjg  Revisions to enable char_size emboldening for PCL 6 emulation.
 *   30-Oct-96 mby  Added FONT_METRICS structure; function prototype for
 *                  CGIFfont_metrics().
 *   07-Nov-96 jfd  Corrected prototype for CGIFmakechar() if !MULTICALLER
 *                  and BOLD_P6.
 *   18-Nov-96 mby  Defined FNT_MET_NAM_LEN for FONT_METRICS structure.
 *   08-Jan-97 dlk  Deleted KERN option as part of project to trim ufst.
 *                  Left structure defn. for KERN_PAIR in tact for now.
 *                  Left CGIFkern() query codes in for now.
 *                  Left CGIFsegments() data segment codes in for now.
 *                  Left Error Return Codes in for now.
 *                  Deleted 'CG macros' for CGIFkern.
 *   13-Jan-97 dlk  Removed CONVERGENT_FONTS option as part of project to
 *                  trim ufst.
 *                  Removed ERR-bad-awt error message define.
 *   12-Mar-97 dlk  Added defines for FC_GALLEY_MASK, and FC_GALLEYSEG_TYPE to
 *                  support Galley Character Segment handling in format 15 & 16
 *                  PCLETTOs.
 *   18-Mar-97 mby  Added new error code, ERRgray_BytestreamNoMem 968.
 *   24-Mar-97 mby  Made 'GalleyFlags' in FONTCONTEXT conditional on TT_PCLEOI.
 *                  In FONTCONTEXT for SCALE_MATRIX != 0, changed 'xPhase',
 *                  'yPhase' to 'numXphases', 'numYphases'.
 *   25-Mar-97 dlk  Updated comment for both instances of 'GalleyFlags' field.
 *   08-May-97 mby  Changed #define FNT_MET_NAM_LEN to 50.
 *   29-May-97 sbm  Add TT_ROM_ACT functionality.
 *   15-Jul-97 awr  Pseudo bold via outlines.
 *   31-Jul-97 sbm  Moved FC_ACT_TYPE to ExtndFlags, now EF_ACT_TYPE.
 *   31-Jul-97 dlk  Moved FC_GALLEYSEG_TYPE to ExtndFlags, now EF_GALLEYSEG_TYPE.
 *                  Deleted GalleyFlags from FONTCONTEXT, and FC_GALLEY_MASK.
 *                  FC_GALLEYSEG_TYPE now = 0x0200.
 *   01-Aug-97 mby  Moved grayscale alignment definitions from graymap.h
 *                  In _matrix_ FONTCONTEXT, removed #if ASIAN ENCODING from
 *                  around ExtndFlags; added #if BOLD || BOLD_FCO around
 *                  xbold & ybold.
 *   08-Aug-97 mby  Added new error Microtype error codes 515, ..., 537
 *   20-Aug-97 mby  Added new error Microtype error code 538
 *   03-Sep-97 keb  Removed references to BJG_BOLD_P6
 *   04-Sep-97 sbm  Modified references to AGFA Compressed TrueType (ACT).
 *   29-Sep-97 mby  Add bit (10) to ExtndFlags in FONTCONTEXT. If set,
 *                  disable TrueType Missing Pixel Recovery. This is to fix
 *                  rasterization problems in fonts with "wrong-way" curves,
 *                  though at the cost of pixel dropouts at small sizes.
 *   08-Oct-97 mby  Change meaning of ExtndFlags / Bit 10. Remove missing
 *                  pixel recovery changes of 9/29/97. This bit now enables
 *                  some special contour polarity checking code in TT_IF.C
 *                  (to handle questionnable font contour data).
 *   21-Nov-97 mby  Added new MicroType error code 539.
 *	 09-Dec-97 slg	Remove obsolete grayscale modes (comment out for now)
 *  13-Jan-98 awr Changed FC_ISOUTLINE() macro- would give wrong results
 *                for grayscaling and anything in the future.
 *  19-Jan-98 awr Added xpower and ypower to IFOUTLINE structure to support
 *                very large characters
 *   30-Jan-98 slg Some IFCONFIG fields only necessary for DISK cases.
 *   03-Feb-98 dlk  Added defines for EF_HP4000_TYPE and macro FC_ISHP4000(fc)
 *                  for HP4000 emulation.
 *    7-Feb-98 awr  Added VLCOUTPUT to CGIFbound_box() prototype.
 *   13-Feb-98 dah  Added EF_LNDSCAPE_TYPE & FC_ISLANDSCP(fc) 
 *   02-Mar-98 dah  Added AGFATOOLS to allow ROM simulation
 *   18-Mar-98 dlk  Changed 'FC_IS..." macros for VERTSUBS and GALLEYSEG to use
 *                  bitwise AND instead of equality check ('==').
 *   25-Mar-98 jfd  Changed typo in last arg of CGIFbound_box() prototype
 *                  from LPSSW16 to LPSW16.
 *   27-Mar-98 jfd  Added error code ERRbanding_not_supported.
 *	 12-Jun-98 slg	Move all fn prototypes to shareinc.h; floating-point error
 *				    codes moved here from fpmath.h
 *   09-Jul-98 dlk  Added error messages in range of 1200 - 1299 for CFF.  Added
 *                  'char  name[64]' field to FONTCONTEXT structure conditionally
 *                  on CFF_RDR (2 places).  Changed copyright notice dates.
 *
 *   10-Jul-98 awr  Added userDesignVector[4] to FONTCONTEXT to support
 *                  multiple master instances
 *   29-Jul-98 dlk  Added error message code range 1300 - 1399 for more T1 and
 *                  CFF input scanning and allocation errors.
 *   05-Aug-98 awr  Changed !ROM to DISK_FONTS. Added FONTCONTEXT.rom_length
 *   31-Aug-98 keb  modified for xl2.0 font processing support 
 *   11-Dec-98 keb  Added Extend Flags EF_VERTWRIT_TYPE and EF_VERTEXSEG_TYPE 
 *                  and xres and yres to MAT0, MAT1 and MAT2 for use with vertical 
 *                  writing code
 *   20-Jan-99 dlk  Added ERR... message defines for CFF 1230 - 1236 processing.
 *   01-Feb-99 dlk  Added ERR_cff_bad_args for CFF procesing.
 *   15-Feb-99 dlk  Added ERR_cff_idx2SID for CFF processing.
 *   16-Feb-99 dlk  Added ERR_cff_chId2cgnum for CFF processing.
 *   09-Mar-99 dlk  Added ERR_WIDTHS_Command for CGIGwidths() processing.
 *   14-May-99 keb  Changed the values of extend flags VERTWRIT and VERTEXSEG so they 
 *                  are not in conflict with VERTSUBS.
 *	 10-Nov-99 slg	Added "ttc_index" field to fontcontext (for TTC files); 
 *					rename EF_CONTCHK_MASK to EF_TT_CONTCHK (as not a mask);
 *					define EF_FONTTYPE_MASK as 8 bits, not 4 (room to expand);
 *					add ERR_TTC_BAD_IDX code (invalid index for TTC file)
 *	 18-Jan-00 slg	Integrate vertical-writing changes (for keb). Remove 
 *					"top_bearing" from IFBITMAP; delete EF_SPECFUNC_MASK and
 *					EF_VERTWRIT_TYPE defines; add EF_XLFONT_TYPE, 
 *					EF_VERTWRIT_MASK, and EF_UFSTVERT_TYPE defines.
 *   31-Jan-00 slg  Integrate disk/rom changes (for jd) - add FC_ISROM and
 *					FC_ISACT macros.
 *	 03-Feb-00 slg	Add EF_TT_NOHINT bit to ExtndFlags (TT hint processing will
 *					be disabled if this bit is set); add FC_DONTHINTTT macro;
 *					modify definitions of most test-ExtndFlags macros (just
 *					test whether bit is set, rather than mask-and-compare).
 *   04-Feb-00 slg  Changed SWP799 to WINDCOMP
 *	 11-Feb-00 aof	Added FC_20THS_TYPE flag and FC_IS8THS(), FC_IS16THS(), and 
 *					FC_IS20THS() macros to support scaling in 1/20ths.
 *   25-Feb-00 ks   Cast BMHEADERSIZE to UL32
 *   21-Mar-00 keb  xres and yres no longer needed for vertwrit in MAT0, MAT1 and MAT2
 *   24-Mar-00 jwd  Cache w/ NO_SYMSET_MAPPING.
 *   28-Sep-00 jfd  Added new error codes for EMBEDDED_BITMAP support;
 *                  In FONTCONTEXT, replaced field 'lpm' with fields 'xlpm'
 *                  and 'ylpm' and removed conditional compile based on
 *                  WINDCOMP because we want unconditional access to fields.
 *   13-Nov-00 jfd  Added new define EF_TT_CMAPTABL for specifying user-supplied CMAP param request.
 *                  Added new macro FC_ISUSERCMAPTBL for testing for user-supplied CMAP param request.
 *                  Added fields "user_platID" and "user_specID" to FONTCONTEXT.
 *   28-Nov-00 slg	Removed error codes / data structures for obsolete options
 *					FCO_TT, PCLEO, PST1, KERN; added 1 new bit to ExtndFlags =
 *					EF_TT_EMBEDBIT to enable TT embedded-bitmap processing.
 *	 04-Dec-00 slg	Remove more unused error codes; redefine the ExtndFlags bit
 *					as EF_TT_NOEMBED (since the default should be for 
 *					embedded-bitmaps to be enabled if ExtndFlags is all-0).
 *	 05-Dec-00 slg  Add nonfatal CGIFtt_cmap_query() errorcode = ERR_TT_TOO_MANY_CMAPS
 *   02-Apr-01 awr  Return error if PS path doesn't start with movetos
 *   19-Apr-01 awr  Added error return for wrong wound contours
 *	 04-May-01 slg  Non-UFST data-type cleanup; get rid of AGFATOOLS hack;
 *					 tighten up conditional compile for embedded bitmaps;
 *					 put back EF_TT_CONTCHK as synonym for EF_CONTCHK.
 *   22-May-01 awr  Added error code ERR_odd_transitions for fast fill
 *   27-Jul-01 jfd/rl/fa Added new macro FC_SE_ANY.
 *   24-Aug-01 jfd	Added define for DL_SYMSET.
 *	 30-Aug-01 slg	Added new field "UL32 optionalThreshold" to FONTCONTEXT: 
 *					if nonzero, this value will be used in place of the hardcoded
 *					PS_THRESHOLD value which determines when missing-pixel recovery
 *					kicks in for PostScript (useful for problematic fonts / fonts
 *					with lots of hairline strokes)
 *   06-Dec-01 jfd  Added new error code ERR_stroke_pct.
 *                  Added new defines STIK_FORMAT, STIK_CHAR_BIT and CD_ISSTIKCHAR.
 *                  Added new field "stroke_pct" to FONTCONTEXT structure.
 *   23-Jan-02 jfd  In FONTCONTEXT structure, expanded conditional compile directive
 *                  surrounding "optionalThreshold" based on (TT_RDR && CFF_RDR).
 *   19-Aug-02 jfd  Added new define EF_TT_NOPLUGS and new macro FC_DONTUSEPLUGINS
 *                        to support disabling of plugins for stroke fonts.
 *   15-Aug-02 awr  Added DIMM_DISK
 *   25-Sep-02 jfd   Added new define ERR_reflines_needed
 *   23-Oct-02 jfd   Removed "xlpm" and "ylpm" fields from FONTCONTEXT structure
 *                        (moved to IF_STATE).
 *   04-Nov-02 jfd   Added missing character substitution support.
 *   12-Jun-03 jfd   Added "cache-by-reference" support.
 *	 26-Apr-04 slg	 Added support for IFBITSPLIT option. 
 *					 Errorcode cleanup: remove unused errorcodes; 
 *						move kanji.h codes here & renumber;
 *						rename fpmath errors to match "ERR*" convention;
 *						include <renamed, renumbered> fserr.h codes here
 */


#ifndef __CGIF__
#define __CGIF__

typedef MEM_HANDLE   HIFFONT;
typedef MEM_HANDLE   HIFBITMAP;
typedef MEM_HANDLE   HIFOUTLINE;
typedef HIFBITMAP  FARPTR * PHIFBITMAP;
typedef HIFOUTLINE FARPTR * PHIFOUTLINE;

/* Bits in CGIFinitstate indicating components not initialized */

#define not_buf   0x0002
#define not_cac   0x0004
#define not_enter 0x0008
#define not_intellifont 0x0010

/* Bits in MTinitstate indicating components not initialized */

#define not_BUCKlru 0x0002
#define not_FNTlru  0x0004
#define not_BMlru   0x0008
/*----------------------------------*/
/*         Memory Pools             */
/*----------------------------------*/
#define CACHE_POOL   0
#define BUFFER_POOL  1
#define CHARGEN_POOL	2

/*----------------------------------*/
/*     CGIFkern() query codes       */
/*----------------------------------*/
#define  TEXT_KERN    0
#define  DESIGN_KERN  1

/*-----------------------------------*/
/* CGIFsegments() data segment codes */
/*-----------------------------------*/
#define FACE_GLOBAL_KEY   0    /* Entire face global segment */
#define GIF_KEY         100    /* Global Intellifont segment */
#define TRACK_KERN_KEY  101    /* track kerning              */
#define TEXT_KERN_KEY   102    /* text kerning               */
#define DESIGN_KERN_KEY 103    /* designer kerning           */
#define WIDTH_KEY       104    /* character width segment    */
#define ATTRIBUTE_KEY   105    /* Attribute header           */
#define RASTER_KEY      106    /* Raster parameter           */
#define TF_HEADER_KEY   107    /* Typeface Header segment    */
#define COMPOUND_KEY    108    /* Compound character         */
#define DISPLAY_KEY     109    /* Display header             */
#define FONT_ALIAS_KEY  110    /* Font Alias segment         */
#define COPYRIGHT_KEY   111    /* Copyright notice           */

/*-------------------------------------*/
/* CGIFfco_Access() data segment codes */
/*-------------------------------------*/

#define TFATRIB_KEY     401    /* Typeface Attribute data    */

/*----------------------------------*/
/*        Error Return Codes        */
/*----------------------------------*/
#define ERR_bad_bitmap_width   1
#define ERR_no_font_index      3   /* can't open font index file */
#define ERR_rd_font_index      4   /* can't allocate or read font index */
#define ERR_missing_plugin     5   /* missing one or more plugin lib    */
#define ERR_no_symbol_set      6   /* can't open symbol set file        */
#define ERR_rd_symbol_set      7   /* can't allocate or read symbol set */
#define ERR_incompatable_plugin 8  /* plugin libs incompatable with fonts*/
#define ERR_incomp_bitmap_width 9  /* bitmap width conflict with RASTER_ORG*/

#ifdef WINDCOMP
#define ERR_nz_bad_slot			11	/* bad index into new NZTRANS->indices[] */
#define ERR_nz_realloc			12  /* unable to re-allocate NZTRANS->nodes[] */
#define ERR_nz_alloc_failed		13	/* unable to allocate NZTRANS */
#endif

#define  ERR_nz_contours_wrong 14  /* Some contours are inconsistant for psuedo bold */
#define  ERR_odd_transitions   15  /* odd number of transitions in a raster line */

#define  ERR_bad_pool          21
#define  ERR_CACinit           32
#define  ERR_IXinit            33
#define  ERRlong_font_name     51
#define  ERR_fst_type          52  /* invalide fst type in fc->format */
#define  ERR_no_font           53
#define  ERR_IXnew_ss          54
#define  ERR_bad_chID          61
#define  ERR_bm_buff           63
#define  ERR_no_cgnum          64  /* ss id does not map to a cgnum */
#define  ERR_no_fi_entry       65  /* no font index */
#define  ERR_open_lib          66  /* Error opening library file */
#define  ERR_mem_face_hdr      67  /* Mallac failed for face header */
#define  ERR_face_abort        68
#define  ERR_find_cgnum        69  /* Can't find cgnum in char. index */
#define  ERR_rd_char_data      70  /* Can't read character data       */
#define  ERR_buffer_too_small  71
#define  ERR_invalid_tile      72  /* App spec'd inside out tile  */
#define  ERR_invalid_band      73  /* App spec'd band with invalid rotation */
#define  ERRnzw_mem_overflow   74  /* nzw buffer was too small */
#define  ERR_font_metrics      89  /* CGIFfont_metrics error           */
#define  ERRinvalid_seg_key    91  /* bad key param in CGIFsegments()  */
#define  ERRmem                92  /* general memory allocation error  */
#define  ERRoutline_mem        93
#define  ERR_ov_16_bit_value   95  /* 16-bit overflow in MAKifbmheader() */

/* CGIFbound_box */
#define ERRfont_bbox          120  /* Error reading font bounding box  */

/*
 * Error codes 130-149 reserved for mapping issues
	(Jis, KSC, Big5, GB, Unicode, etc) 
 */

#define ERR_INVALID_MAPPING			130
#define ERR_JISTOUNI_READ           131
#define ERR_UNITOJIS_READ           132
#define ERR_KSCTOUNI_READ           133
#define ERR_UNITOKSC_READ           134
#define ERR_BIG5TOUNI_READ          135
#define ERR_UNITOBIG5_READ          136
#define ERR_GBTOUNI_READ            137
#define ERR_UNITOGB_READ            138

#if IF_RDR
/* fm.c */
#define  ERR_if_read_libfile 150    /* dynamic fonts, read error */
#define  ERR_if_process_face 151    /* dynamic fonts, invalid libr format */
#endif  /* IF_RDR */

#if CACHE
/* cache.c */
#define  ERR_char_not_cached       160	/* cache-by-reference, char not cached  */

/* cgif.c */
#define  ERR_ref_counter_overflow  161	/* cache-by-reference, > MAX_ULONG references */
#endif

#if PST1_RDR
/*
PostScript Input error messages (300 - 399, plus 1200 - 1399)
*/
/*---------*/
#define ERR_psi_scan_file    300	/* error scanning type 1 file */
#define ERR_psi_scan_matrix  301	/* error scanning for type 1 matrix */
#define ERR_psi_rd_bhdr      302	/* error reading type 1 binary header */
#define ERR_psi_scan_private 303	/* error scanning for type 1 private */
#define ERR_psi_scan_LenIV   304	/* error scanning for type 1 LenIV */
#define ERR_psi_scan_StdVW   305	/* error scanning for type 1 StdVW */
#define ERR_psi_scan_StdHW   306	/* error scanning for type 1 StdHW */
#define ERR_psi_scan_Blues   307	/* error scanning for type 1 Blues */
#define ERR_psi_scan_OBlues  308	/* error scanning for type 1 Other Blues */
#define ERR_psi_scan_BScale  309	/* error scanning for type 1 BlueScale */
#define ERR_psi_scan_BShift  310	/* error scanning for type 1 BlueShift */
#define ERR_psi_scan_Encode  311	/* error scanning for type 1 Encoding */
#define ERR_psi_scan_Subrs   312	/* error scanning for type 1 Subrs */
#define ERR_psi_Subr_too_hi  313	/* Type 1 Subr # too high */
#define ERR_psi_scan_Charstr 314	/* error scanning for type 1 Charstrings */
#define ERR_psi_Char_too_hi  315	/* Type 1 Charstring # too high */
#define ERR_psi_SubrStr_mem  316	/* error alloc'g type 1 Subr array */
#define ERR_psi_Subrdat_mem  317	/* error alloc'g type 1 Subr data mem */
#define ERR_psi_CharStr_mem  318	/* error alloc'g type 1 Charstring array */
#define ERR_psi_Charname_mem 319	/* error alloc'g type 1 Char name mem */
#define ERR_psi_Chardat_mem  320	/* error alloc'g type 1 Char data mem */
#define ERR_psi_Encode_mem   321	/* error alloc'g type 1 Encoding array mem */
#define ERR_psi_Encodename_mem 322	/* error alloc'g type 1 Encoding name mem */
#define ERR_psi_open_file    324	/* error opening type 1 file */
#define ERR_psi_rdfont       325	/* error reading type 1 font */
#define ERR_psi_cc_missing   326 /* piece of compound char missing */
#define ERR_psi_hint_ovflow  327 /* hint structure overflow */
#define ERR_psi_wkbuf_ovflow 328 /* working buff overflow */
#define ERR_psi_pcleo_encr   331 /* encrypted ps pcleos unsupported */
#define ERR_psi_pcleo_nodata 332 /* unable to find ps pcleo char data */
#define ERR_psi_cg2idx       333 /* unable to map cgnum to index */
#define ERR_psi_idx2psname   334 /* unable to map index to PS name */
#define ERR_psi_pcleo_not1   335 /* pcleo not type 1 */
#define ERR_psi_scan_StemSnapH 337	/* error scanning for type 1 StemSnapH */
#define ERR_psi_scan_StemSnapV 338	/* error scanning for type 1 StemSnapV */
#define ERR_psi_WeightVector 339	/* error reading type 1 WeightVector */
#define ERR_psi_MMstdvw     340	/* error reading type 1 MMstdvw */
#define ERR_psi_MMstdhw     341	/* error reading type 1 MMstdhw */
#define ERR_psi_MMblendtypes 342	/* error reading type 1 MMblendtypes */
#define ERR_psi_alloc_MMblendmap 343	/* error alloc'g type 1 MMblendmap[] */
#define ERR_psi_MMblendmap 344	/* error reading type 1 MMblendmap[] */
#define ERR_psi_alloc_MMblues 345	/* error alloc'g type 1 MMblues[] */
#define ERR_psi_alloc_MMotherblues 346	/* error alloc'g type 1 MMotherblues[] */
#define ERR_psi_badMMblendmap 347   /* bad multimaster blendmap */
#define ERR_psi_scan_ps_font_bbox 348 /* error scanning for type 1 FontBBox */
#define ERR_psi_too_many_hints 350  /* Exceeded maximun number of hints  */
#define ERR_psi_bad_hint_val    351 /* invalid negative number           */
#define ERR_cff_not_enabled 352 /* processing CFF file with CFF_RDR == 0 */
#define ERR_psi_alloc_MMblendpos  353  /* error allocating memory for MM BlendDesignPositions */
#define ERR_psi_MMblendpos 354  /* error reading MM BlendDesignPosition array */
#define ERR_psi_alloc_MMbluescales 355  /* error allocating memory for MMbluescales */
#define ERR_psi_MMbluescales       356  /* error assigning values to MMbluescale array */
#define ERR_psi_alloc_MMblueshifts 357  /* error allocating memory for MMblueshifts */
#define ERR_psi_MMblueshifts       358  /* error assigning values to MMblueshifts array */
#define ERR_psi_alloc_MMstemsnapsH 359 /* error allocating memory for MMstemsnapsH[] */
#define ERR_psi_CidSubrStr_mem     360	/* error alloc'g type 1 CID Subr array */
#define ERR_psi_CidSubrDat_mem     361	/* error alloc'g type 1 CID Subr data mem */
#define ERR_psi_CidChardat_mem     362  /* error alloc'g type 1 CID Char data mem */
/*
More MM and CFF codes are located in the 1300 - 1399 range
*/

#define ERR_psi_no_moveto          376 /* a path does not start with a moveto */

/* Type 1 or CFF stack errors encountered (when PS_ERRORCHECK option enabled) */
#define ERR_psi_stack_underflow	377		/* attempting to pop more than you've pushed */
										/* (probably signalling a corrupted font) */
#define ERR_psi_stack_overflow	378		/* attempting to push too much data onto the stack */
										/* (probably signalling a corrupted font) */
#endif /* PST1_RDR */


#if CFF_RDR
/*
Error codes 1200 through 1299 reserved for PostScript CFF & Type 2 errors
*/
#define ERR_cff_bad_face_name   1200  /* type name not in fontset          */
#define ERR_cff_T2_arg_cnt      1201  /* T2Char invalid argument count     */
#define ERR_cff_enc_arr_mem     1202  /* mem alloc fail for encode array   */
#define ERR_cff_bad_offset_size 1203  /* must be 1-4                       */
#define ERR_cff_no_str_mem      1204  /* Bad alloc in get charstrings      */
#define ERR_cff_no_readmm_mem   1205  /* Bad alloc in read_MM              */
#define ERR_cff_bad_SID_val     1207  /* SID must be > 390                 */
#define ERR_cff_bad_read        1208  /* bad file read                     */
#define ERR_cff_bad_read_len    1209  /* invalid length for buffer read    */
#define ERR_cff_no_dict_mem     1210  /* Bad alloc in get top dict         */
#define ERR_cff_T2_Store_err    1215  /* T2char store operator bad args    */
#define ERR_cff_T2_Load_err     1216  /* T2char load operator bad args     */
#define ERR_cff_T2_Put_err      1217  /* T2char put operator no dest       */
#define ERR_cff_T2_Get_err      1218  /* T2char put operator no source     */
#define ERR_cff_T2_Index_err    1219  /* T2Char index, out of range index  */
#define ERR_cff_bad_synth       1222  /* synthetic font value out of range */
#define ERR_cff_no_support      1223  /* unsupported feature               */
#define ERR_cff_bad_encode      1224  /* bad encoding file position        */
#define ERR_cff_bad_str_pos     1225  /* bad charstrings file position     */
#define ERR_cff_bad_str_mem     1226  /* no charstrings memory             */
#define ERR_cff_ROS_notsupp     1227  /* ROS fonts not supported           */
#define ERR_cff_Chamel_notsupp  1228  /* Chameleon fonts not supported     */
#define ERR_cff_get_string_data 1229  /* Get text or data string failed    */
#define ERR_cff_bad_charset     1230  /* Charset read failure in Top DICT  */
#define ERR_cff_charset_arr_mem 1233  /* Charset mem not properly set up   */
#define ERR_cff_cg2idx          1235  /* Unable to get index for cgnum     */
#define ERR_cff_chname2SID      1236  /* Unable to convert PSchname to SID */
#define ERR_cff_bad_args        1237  /* Wrong number of arguments, or bad */
#define ERR_cff_idx2SID         1238  /* Unable to get SID for mapidx      */
#define ERR_cff_chId2cgnum      1239  /* Unable to get cgnum for chId in SS*/
#define ERR_cff_FDSelect_mem 	1240  /* FDSelect mem not properly set up  */
#define ERR_cff_FDSelect_fmt	1241  /* FDSelect format not supported     */

#endif /* CFF_RDR */



#if PST1_RDR
/*
Codes 1300 - 1399 Reserved For Continuation of 300 - 359 CFF and MM Errors
*/
#define ERR_psi_alloc_MMstemsnapsV 1300  /* error allocating memory for MMstemsnapsV[] */
#define ERR_psi_alloc_MMfontbbox   1301  /* error allocating memory for Mfontbbox[] */
#define ERR_psi_scan_force_bold_threshold 1302 /* eror scanning for ForceBoldThreshold value */
#define ERR_psi_scan_forcebold     1303  /* error scanning for ForceBold value */
#define ERR_psi_alloc_MMforcebold  1304  /* error allocating memory for MMforcebold values */
#define ERR_psi_MMforcebold        1305  /* error reading MM ForceBold values */

#define ERR_psi_generic            1399  /* PSI and CFF generic error */
#endif /* PST1_RDR */


#if TT_RDR
/*
 * TrueType error codes -- tt_if.c
 */
/** from findPcleoHdrEntry */
#define PCLErr_Fontname_NOT_FOUND 380 /* EFM font does not contain font name specfied */
#define ERR_TT_OPENDISK      400 /* load_font: can't open disk font file. */
#define ERR_TT_BADCONFIG     402 /* load_font: attempt to load font type not enabled in cgconfig.h */
#define ERR_TT_FSOPEN        404 /* load_font: fs_OpenFonts()  */
#define ERR_TT_FSINIT        405 /* load_font: fs_Initialize() */
#define ERR_TT_FSNEWSFNT     406 /* load_font: fs_NewSfnt()    */
#define ERR_TT_FSNEWTRANS    407 /* set_trans: fs_NewTransformation()  */
#define ERR_TT_FSGRIDFIT     408 /* make_gaso... : fs_ContourGridFit() */
#define ERR_TT_PCLEONOGT     410 /* global TT data segment not found */
#define ERR_TT_PCLEONOMEM    411 /* not enough memory to load global TT data */
#define ERR_TT_ROMPTR        412 /* ROM font pointer is NULL */
#define ERR_TT_DISKREAD      413 /* disk read or seek error */
#define ERR_TT_FSCHARBEARINGS 414 /* make_gaso... : fs_FindCharBearings() */
#define ERR_TTC_BAD_IDX		 418 /* font index within TTC file out-of-range */ 

#if EMBEDDED_BITMAPS && TT_RDR && CGBITMAP
#define ERR_TTEMB_INVIMAGFORMAT		420	/* invalid image format for embedded bitmap */
#define ERR_TTEMB_IMAGFORMATNOTSUPP	421	/* embedded bitmap image format 3 or 4 not supported */
#define ERR_TTEMB_MISSINGGLYPH		422	/* embedded bitmap composite has missing glyph */
#endif	/* EMBEDDED_BITMAPS && TT_RDR && CGBITMAP */

#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)	/* 12-05-01 jfd */
#define ERR_stroke_pct       423
#define ERR_stik_extract_outline    424
#define ERR_reflines_needed  426
#endif	/* STIK && (TT_DISK || TT_ROM || TT_ROM_ACT) */

/* CGIFtt_cmap_query() error */
#define ERR_TT_TOO_MANY_CMAPS		433 /* too many cmap subtables to fit into
										preallocated query structure - this is
										a non-fatal error, because some cmap
										info will still be returned */

#define	ERR_TT_NO_GLYF_TABLE		440

#endif  /* TT_RDR */

#if TT_RDR || FCO_RDR
#define ERR_TT_MEMFAIL       403 /* load_font: not enough buffer memory. */
#endif

#if FCO_RDR
/*
 * Font Collection Object error codes
 */
#define ERR_fco_FCnew           500
#define ERR_fco_NoMem           503
#define ERR_fco_FCcompCharPiece 505
#define ERR_fco_intelliflator   506
#define ERR_fco_handle          507
#define ERR_fco_versionError    509
#define ERR_fco_SegmentSharing  510
#define ERR_fco_Init            511
#define ERR_fco_nzwCode         512
#define ERR_fco_OutlCode        513
#define ERR_fco_FCnew_CurveData      514   /* Either missing, invalid or bad curve data */
#define ERR_fco_FCnew_FileOpen       515   /* File error */
#define ERR_fco_FCnew_Memory         516   /* out of memory error (BUFalloc) */
#define ERR_fco_FCnew_CorruptData    517   /* corrupted FCO data */
#define ERR_fco_FontNew_Memory       518
#define ERR_fco_FontNew_CorruptData  519
#define ERR_fco_CharNew_Memory       520
#define ERR_fco_CharNew_CorruptData  521
#define ERR_fco_ModelNew_Memory      522
#define ERR_fco_ModelNew_CorruptData 523
#define ERR_fco_ChmapNew_CorruptData 524
#define ERR_fco_ChmapNew_Memory      525
#define ERR_fco_CvsegNew_Memory      526
#define ERR_fco_InflNew_Memory       527
#define ERR_fco_FInfo_CorruptData    528
#define ERR_fco_TableNew             536
#define ERR_fco_TableOffset          537
#define ERR_fco_FCcompCharShort      538
#define ERR_fco_FCccMissingPiece     539
#endif  /* FCO_RDR */

/*---------*/
#define  ERR_bm_gt_oron       599  /* Char bitmap too big for on array */
#define  ERR_bm_too_big       600  /* Char bitmap too big */
#define  ERR_mem_char_buf     602  /* Can't malloc character buffer */
#define  ERRmatrix_range      603  /* scaling outside of range */
#define  ERRdu_pix_range      604  /* scaling outside of range */
#define  ERR_if_init_glob     605  /* if_init_glob() failed */
#define  ERR_comp_pix         606  /* com_pix() failed */
#define  ERR_fixed_space      607  /* character is a fixed space */
#define  ERRsingular_matrix   608  /* scaling matrix has 0 determinate */
#define  ERRDLBmpInProcess    609  /* Processing PCL DL bitmaps jwd, 07/21/03 */
#define  ERR_skeletal_init    610
#define  ERR_y_skel_proc      611
#define  ERR_x_skel_proc      612
#define  ERR_no_contour       701
#define  ERR_not_hq3          702
#define  ERR_ov_char_buf      703
#define  ERR_out_of_handles   800
#define  ERR_bad_handle       801
#define  ERR_lost_mem         802  /* CGIFdefund() didnot free whole block */
#define  ERR_IXopen_file      803
#define  ERR_no_buck_mem      804  /* BUCKnew couldn't get memory */
#define  ERR_cc_complex       805  /* Compound character hs too many parts */
#define  ERR_no_cc_part       806  /* Can't find compound character part */
#define  ERR_missing_block    807  /* CGIFdefund() can't find the block */
#define  ERRtoo_many_dup_skel 904  /* too many duplicate skel points    */
#define  ERRtoo_many_loops    906  /* too many contour loops in character */

/*  output processor installation errors */
#define  ERRinvalid_outproc_type     950
#define  ERR_quadratic_not_installed 953
#define  ERR_linear_not_installed    954
#define  ERR_cubic_not_installed     955

/* Grayscaling */
#define  ERRgray_parm_not_supported  960 /* gray scaling parameter */
#define  ERRgraymaptoobig            963
#define  ERRnotenoughprecision       965
#define  ERRgray_if_not_supported    966  /* No Intellifont */
#define  ERRchar_size_not_supported  967  /* CGIFchar_size() not supported */
#define  ERRgray_BytestreamNoMem     968

/* Banding */
#define  ERRbanding_not_supported    970  /* Banding not supported with  */
                                          /* SCALE_MATRIX != 0           */

/* Compressed cache */
#define ERRcmpr_too_complex      1000
#define ERRcmpr_data             1001
#define ERRcmpr_notimplemented   1002
#define ERRcmpr_notcompressed    1003

/* ACT error codes, from errcodes.h  Use 1020-1099*/
#define ERR_no_more_memory				1020
#define ERR_new_logical					1021
#define ERR_realloc_failed				1022
#define ERR_realloc_bad_pointer			1023
#define ERR_mem_dangling_pointers		1024
#define ERR_delete_bad_pointer			1025
#define ERR_mem_Create_Failed			1026
#define ERR_array_start_thrashed		1027
#define ERR_array_end_thrashed			1028
#define ERR_bad_ACT_version				1050

#define ERRsmear_end_loop         	1102
#define ERRbold_horizontal_value_too_big 1103
#define ERRbold_vertical_value_too_big 1104

/* LINKED FONT error code */
#if LINKED_FONT
#define ERR_too_many_fonts				2020
#endif

/* Codes 2030 - 2049: mutex error codes */
#if UFST_MULTITHREAD
#define ERR_BAD_REF_COUNT				2030
#define ERR_MUTEX_CREATE				2031
#define ERR_MUTEX_GONE					2032
#define ERR_MUTEX_TIMEOUT				2033

#define ERR_creating_mutex				2040
#define ERR_obtaining_mutex				2041
#define ERR_releasing_mutex				2042
#define ERR_deleting_mutex				2043
#endif
/* Codes 2050 - 2059: multithread error codes */
#if UFST_MULTITHREAD
#define ERR_max_threads_exceeded		2050
#define ERR_thread_not_found_in_list	2051
#endif
/*** error codes 3000-3999 reserved for TrueType ***/

/* These additional TrueType error codes are defined in fserror.h:
the error codes were renamed to match our ERR_* convention,
and were renumbered to fit into existing cgif.h ranges */

#if TT_RDR
#include "fserror.h"
#endif

/**** for reference only: defined in fserror.h
#define ERR_TT_NULL_KEY						3001
#define ERR_TT_NULL_INPUT_PTR				3002
#define ERR_TT_NULL_SFNT_DIR				3003
#define ERR_TT_NULL_OUTPUT_PTR				3004
#define ERR_TT_INVALID_GLYPH_INDEX			3005
#define ERR_TT_UNDEFINED_INSTRUCTION		3006
#define ERR_TT_POINTS_DATA					3007
#define ERR_TT_CONTOUR_DATA					3008
#define ERR_TT_BAD_MAGIC					3009
#define ERR_TT_OUT_OF_RANGE_SUBTABLE		3010
#define ERR_TT_UNKNOWN_COMPOSITE_VERSION	3011
#define ERR_TT_CLIENT_RETURNED_NULL			3012
#define ERR_TT_MISSING_SFNT_TABLE			3013
#define ERR_TT_UNKNOWN_CMAP_FORMAT			3014
**************************************************/

/* error code 5000 - 5100 reserved for type checking 
   ( function check_types() ) */
#define ERR_BYTE_ORDER			5000
#define ERR_SB8_TYPE			5001
#define ERR_UB8_TYPE			5002
#define ERR_SW16_TYPE			5003
#define ERR_UW16_TYPE			5004
#define ERR_SL32_TYPE			5005
#define ERR_UL32_TYPE			5006
#define ERR_SL64_TYPE			5007
#define ERR_UL64_TYPE			5008
#define ERR_SIZEOF_LONG			5009
#define ERR_INT_64				5010
#define ERR_MULTI_REENTRANT		5011
/* Floating-point */
#define ERR_FPadd_overflow       9001
#define ERR_FPmultiply_overflow  9003
#define ERR_FPdivide_by_zero     9005
#define ERR_FPdivide_overflow    9006
#define ERR_FPsqrt_neg           9007

#define  ERR_char_unavailable  0x7FFF  /* "special" code returned by CGIFwidth() */

/*----------------------------------*/
/* Intellifont Configuration Block  */
/*----------------------------------*/

typedef struct
{
    UW16  bit_map_width;   /* BYTE alignment of char bitmap: 1, 2 or 4 */
#if DISK_FONTS
    UW16  num_files;       /* max number of open library files         */
    SB8   ufstPath[PATHNAMELEN];  /* location of ufst files.       */
    SB8   typePath[PATHNAMELEN];    /* location of typeface files.     */
#endif
#if CACHE
    UW16  max_char_size;          /* max cached character bitmap size  */
#endif
#if CHAR_SIZE
#if (HUGE_PTR_SUPPORT)
    HPUB8 cc_buf_ptr;             /* compound character buffer pointer */
#else
    LPUB8 cc_buf_ptr;             /* compound character buffer pointer */
#endif
    SL32  cc_buf_size;            /*    "        "        "     size   */
#endif
} IFCONFIG;
typedef IFCONFIG FARPTR * PIFCONFIG;




/*----------------------------------*/
/*       Font Context               */
/*----------------------------------*/
/*  Masks and field values for FONTCONTEXT format field

    UW16   format;

        output format
        bit 15-14    0  Auto-quality
                     1  quality level 1
                     2  quality level 2
                     3  quality level 3

        Bits 13-12  Font Type
        Value  (in word)        If Bit 5 is 1, then ignore these bits

          0    (0x0000)  Intellifont
          1    (0x1000)  PostScript Type 1
          2    (0x2000)  TrueType
          3    (0x3000)  Font Collection Object

        Bit 11 Font Media for Internal Fonts
        Value
          0    (0x0000)  Disk
          1    (0x0800)  ROM

        Bit 10 Font Source
        Value
          0    (0x0000)  Internal Font
          1    (0x0400)  External Font

        Bits 9-8    FONTCONTEXT.format point size
                    units descriptors
          Value (in word)
            0    (0x0000)  8ths of a point
            1    (0x0100)  16ths of a point
            2    (0x0200)  20ths of a point

        Bit 7  Resolution units
          Value
            0    (0x0000)  Pixels per meter
            1    (0x0080)  Pixels per inch

        Bit 6  (0x0040)  PCL 6 Emulation flag
          Value
            1  (0x0040)  ENABLE PCL 6 emulation (bucket searching & char widths)
            0  (0x0000)  DISABLE PCL 6 emulation

        Bit 5  (0x0020)  Alternate Font Type (for future expansion - for now
          Value              this is always 0)
            0  (0x0000)  Bits 13-12 describe the font type.
            1  (0x0020)  Font type is described in the FONTCONTEXT format2
                         field. Ignore bits 12 & 13.

        Bit 4  (0x0010)  Nonzero Winding
          Value
            1  (0x0010)  Scan conversion using nonzero winding.
            0  (0x0000)  Scan conversion without nonzero winding (even/odd)

        Bits 3-0  Output character type
          Value  (in word)
            0    (0x0000)  Bitmap
            1    (0x0001)  Linear outline
            2    (0x0002)  Quadratic outline
            3    (0x0003)  Cubic outline
            5    (0x0005)  Graymap output
            6    (0x0006)  Transition list output
           15    (0x000f)  Application Generated Bitmap (FC_APPGEN_TYPE)
*/

#define DL_SYMSET          0x8000   /* ssnum downloadable symbol set flag */ /* 08/16/01, jwd */
#define FC_CHANGESCALE     0x3380   /* diff bits => change scale params   */

#define FC_EXTERN_TYPE     0x0400   /* PCLEO file format                  */
#define FC_ROM_TYPE        0x0800   /* ROM based file                     */

#define FC_FONTTYPE_MASK  0x3000
#define FC_IF_TYPE        0x0000   /* Intellifont                        */
#define FC_PST1_TYPE      0x1000   /* PostScript Type 1 font             */
#define FC_TT_TYPE        0x2000   /* TrueType font                      */
#define FC_FCO_TYPE       0x3000   /* Font Collection Object */
#define FC_ALTERNATE_TYPE 0x0020   /* For alternate font type            */

#if LINKED_FONT
/* 0x3c20 = 0011,1100,0010,0000, bits with 1 are those taken from the member font format */
/* 0xc3df = 1100,0011,1101,1111 = ~0x3c20, 1 bits are those not used by user for format */
#define  BITS_USED_IN_FORMAT	0x3c20
#endif

#define EF_FONTTYPE_MASK  0x0000003F    /* ExtndFlags FontType (input) mask.  */
#define EF_FORMAT16_TYPE  0x00000001    /* ExtndFlags mask, format 16 PCLETTO */
#define EF_ACT_TYPE       0x00000002    /* ExtndFlags mask, AGFA Compressed TrueType Object */
#define EF_HP4000_TYPE    0x00000004    /* ExtndFlags mask, HP4000 emulation. */
#define EF_XLFONT_TYPE    0x00000008    /* ExtndFlags mask, XL PCLETTO */
#define EF_DIMM_DISK_TYPE 0x00000010    /* Font is in special memory that uses disk i/o */
#define	EF_PSETTO_TYPE	  0x00000020	/* PS TT incremental download */

#define EF_TT_NOPLUGS     0x00000040    /* ExtndFlags bit to disable plugin support for STIK fonts */
										/* (compare with EF_GLOBAL_NOPLUGS, below) */
#define EF_SUBSTHOLLOWBOX_TYPE 0x00000080	/* ExtndFlags bit to substitute hollow box character for missing char */
#define EF_VERTWRIT_MASK  0x00000F00    /* ExtndFlags Vertical Writing mask. */
#define EF_UFSTVERT_TYPE  0x00000100    /* Internal ExtndFlag */
#define EF_VERTSUBS_TYPE  0x00000200    /* ExtndFlags mask, Vertical Subst.   */
#define EF_GALLEYSEG_TYPE 0x00000400    /* ExtndFlags mask, Galley Character Segment */
#define EF_VERTEXSEG_TYPE 0x00000800    /* ExtndFlags mask, Vertical Exclude Segment */

#define EF_CONTCHK        0x00001000    /* ExtndFlags bit to enable contour polarity checking */
#define EF_TT_CONTCHK     0x00001000    /* ExtndFlags bit to enable contour polarity checking (TT) */
									    /* (synonym for EF_CONTCHK from previous implementation) */
#define EF_TT_NOHINT	  0x00002000	/* ExtndFlags to disable TT hint processing */
#define EF_CID_TYPE1	  0x00004000	/* ExtndFlags bit to signal cid type 1 fonts */
#define EF_TT_NOEMBED	  0x00008000	/* ExtndFlags bit to disable embedded-bitmap option (TT) */

#define LINKED_FONT_TYPE		0x00010000

#if LINKED_FONT
/* 0xFF3B = 1111,1111,0011,1011, bits with 1 are those taken from the member font ExtndFlags  */
/* 0x00C4 = 0000,0000,1100,0100 = ~0xFF3B,  */
#define  BITS_USED_IN_ExtndFlags	0x0000FF3B
#endif

/*** 16 more bits available in ExtndFlags now ***/

#define EF_GLOBAL_NOPLUGS	0x00100000	/* ExtndFlags bit to disable plugin support for all font types */
										/* (compare with EF_TT_NOPLUGS, above) */

#define EF_NOSYMSETMAP		0x01000000	/* New mechanism for triggering NO_SYMSET_MAPPING = set this bit */

#define EF_FORCE_MPR		0x00200000 /* New mechanism for forcing Missing Pixel Recovery */ 

#define EF_NOUSBOUNDBOX     0x00400000 /* ExtndFlags bit to DISABLE USBOUNDBOX option */ /* keb 6/1/06 */

#define FC_ISTTFMT16(fc) ((fc)->ExtndFlags & EF_FORMAT16_TYPE)
#define FC_ISACT(fc)     ((fc)->ExtndFlags & EF_ACT_TYPE)
#define FC_ISHP4000(fc)  ((fc)->ExtndFlags & EF_HP4000_TYPE)
#define FC_ISXLFONT(fc)  ((fc)->ExtndFlags & EF_XLFONT_TYPE)
#define FC_ISDIMM_DISK(fc)  ((fc)->ExtndFlags & EF_DIMM_DISK_TYPE)

#define FC_ISVERTWRIT(fc)	((fc)->ExtndFlags & EF_VERTWRIT_MASK)
#define FC_ISUFSTVERT(fc) ((fc)->ExtndFlags & EF_UFSTVERT_TYPE )
#define FC_ISVERTSUB(fc) ((fc)->ExtndFlags & EF_VERTSUBS_TYPE )
#define FC_ISGALLEYSEG(fc) ((fc)->ExtndFlags & EF_GALLEYSEG_TYPE )
#define FC_ISVERTEXSEG(fc)  ((fc)->ExtndFlags & EF_VERTEXSEG_TYPE ) 

#define FC_ISCONTCHK(fc) ((fc)->ExtndFlags & EF_CONTCHK)
#define FC_DONTHINTTT(fc) ((fc)->ExtndFlags & EF_TT_NOHINT)
#define FC_FORCE_MPR(fc) ((fc)->ExtndFlags & EF_FORCE_MPR )
#define FC_NOUSBOUNDBOX(fc) ((fc)->ExtndFlags & EF_NOUSBOUNDBOX ) /* keb 6/1/06 */

/* original implementation (using ExtndFlags bit) replaced by	*/
/* new implementation (using special SSNUM value) 				*/
#define FC_ISUSERCMAPTBL(fc) ((fc)->ssnum == USER_CMAP)

#define FC_STIKNOPLUGINS(fc) ((fc)->ExtndFlags & EF_TT_NOPLUGS)
#define FC_NOPLUGINS(fc) ((fc)->ExtndFlags & EF_GLOBAL_NOPLUGS)
#define FC_NO_SSMAP(fc) ((fc)->ExtndFlags & EF_NOSYMSETMAP)
#define FC_SSMAP(fc) !((fc)->ExtndFlags & EF_NOSYMSETMAP)

#define FC_ISHOLLOWBOXSUBST(fc) ((fc)->ExtndFlags & EF_SUBSTHOLLOWBOX_TYPE )

#if LINKED_FONT
#define	FC_ISLINKEDFONT(fc)		((fc)->ExtndFlags & LINKED_FONT_TYPE )
#endif

#define FC_ISROM(fc)    ( (fc)->format & FC_ROM_TYPE)
#define FC_ISEXTERN(fc) ( (fc)->format & FC_EXTERN_TYPE)
#define FC_ISIF(fc)    (( (fc)->format & FC_FONTTYPE_MASK ) == FC_IF_TYPE)
#define FC_ISPST1(fc)  (( (fc)->format & FC_FONTTYPE_MASK ) == FC_PST1_TYPE)
#define FC_ISTT(fc)    (( (fc)->format & FC_FONTTYPE_MASK ) == FC_TT_TYPE)
#define FC_ISFCO(fc)   (( (fc)->format & FC_FONTTYPE_MASK ) == FC_FCO_TYPE)
#define FC_ISALTERNATE(fc) ( (fc)->format & FC_ALTERNATE_TYPE)

#define FC_PSUNITS_MASK   0x0300   /* Pt sz units bits w/in format fld   */
#define FC_8THS_TYPE      0x0000   /* Pt sz in 8ths (default)            */
#define FC_16THS_TYPE     0x0100   /* Pt sz in 16ths                     */
#define FC_20THS_TYPE     0x0200   /* Pt sz in 20ths   AOF Feb. 11, 2000 */

/*AOF Feb 11, 2000    Added three macros to support 1/20th changes.      */
#define FC_IS8THS(fc)   (((fc)->format & FC_PSUNITS_MASK ) == FC_8THS_TYPE)
#define FC_IS16THS(fc)  (((fc)->format & FC_PSUNITS_MASK ) == FC_16THS_TYPE)
#define FC_IS20THS(fc)  (((fc)->format & FC_PSUNITS_MASK ) == FC_20THS_TYPE)

#define FC_INCHES_TYPE    0x0080   /* Output resolution is in pixels/inch */

#define FC_PCL6_EMU       0x0040

#define FC_NON_Z_WIND     0x0010   /* non-zero winding scanning method   */

#define FC_OUTPUT_MASK    0x000F   /* output type bits w/in format field */
#define FC_BITMAP_TYPE    0x0000   /* bitmap                             */
#define FC_LINEAR_TYPE    0x0001   /* linear outline                     */
#define FC_QUAD_TYPE      0x0002   /* quadratic outline                  */
#define FC_CUBIC_TYPE     0x0003   /* cubic outline                      */
#define FC_GRAY_TYPE      0x0005   /* graymap align to subpixels         */
#define FC_APPGEN_TYPE    0x000F   /* application generated              */

#define FC_ISNONZWIND(fc) ( (fc)->format & FC_NON_Z_WIND )
#define FC_ISPCL6EMU(fc)  ( (fc)->format & FC_PCL6_EMU )

#define FC_ISBITMAP(fc) (( (fc)->format & FC_OUTPUT_MASK ) == FC_BITMAP_TYPE)
#define FC_ISLINEAR(fc) (( (fc)->format & FC_OUTPUT_MASK ) == FC_LINEAR_TYPE)
#define FC_ISQUAD(fc)   (( (fc)->format & FC_OUTPUT_MASK ) == FC_QUAD_TYPE)
#define FC_ISCUBIC(fc)  (( (fc)->format & FC_OUTPUT_MASK ) == FC_CUBIC_TYPE)
#define FC_ISGRAY(fc)   (( (fc)->format & FC_OUTPUT_MASK ) == FC_GRAY_TYPE)
/* AWR: Was #define FC_ISOUTLINE(fc) (( (fc)->format & 3 ) && !( (fc)->format & 0x000c )) */
#define FC_ISOUTLINE(fc) ( (((fc)->format & FC_OUTPUT_MASK) == FC_LINEAR_TYPE )\
                        || (((fc)->format & FC_OUTPUT_MASK) == FC_QUAD_TYPE )\
                        || (((fc)->format & FC_OUTPUT_MASK) == FC_CUBIC_TYPE ) )

#define FC_ISAPPGEN(fc) (( (fc)->format & FC_OUTPUT_MASK ) == FC_APPGEN_TYPE)

#if GRAYSCALING  /* constants for grid alignment, see 'alignment' in FONTCONTEXT */
#define  GAGG     0   /* GRID align in X, GRID in Y */
#define  GAGH     1   /* GRID align in X, HALFGRID in Y */
#define  GAGP     2   /* GRID align in X, PHASE (or subpixel) align in Y */
#define  GAHG     3   /* HALFGRID align in X, GRID in Y */
#define  GAHH     4   /* ... you get the idea */
#define  GAHP     5
#define  GAPG     6
#define  GAPH     7
#define  GAPP     8

/* make sure all refs to these equates are removed from code... */
/* #define  GASUB    9  */ /* obsolete */
/* #define  GASUPER  10 */ /* obsolete */
/* #define  GAHALF   11 */ /* obsolete */

#endif

#if SPECIAL_EFFECTS

#define FC_SE_ENGRAVE	0x0001	/* Engrave effect*/
#define FC_SE_EMBOSS	0x0002  /* Emboss effect*/
#define FC_SE_SHADOW	0x0004  /* Shadow effect*/
#define FC_SE_OUTLINE	0x0008  /* Outline effect */
#define FC_SE_OUTLINE_SHADOW 0x0010	/* Combined Outline with Shadow */


#define FC_SE_ISENGRAVE(fc)	(((fc)->EffectsFlags)& FC_SE_ENGRAVE)
#define FC_SE_ISEMBOSS(fc)	(((fc)->EffectsFlags)& FC_SE_EMBOSS)
#define FC_SE_ISSHADOW(fc)	(((fc)->EffectsFlags)& FC_SE_SHADOW)
#define FC_SE_ISOUTLINE(fc)	(((fc)->EffectsFlags)& FC_SE_OUTLINE)
#define FC_SE_ISOUTLINE_SHADOW(fc)	(((fc)->EffectsFlags)& FC_SE_OUTLINE_SHADOW)
#define FC_SE_ANY(fc)		((fc)->EffectsFlags)
#endif /* SPECIAL_EFFECTS */

#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)	/* 12-06-01 jfd */
#define STIK_FORMAT     (UW16)0x9654
#define STIK_CHAR_BIT   0x80
#define CD_ISSTIKCHAR(cd)	(((*cd) & STIK_CHAR_BIT) == 0)
/* for STIK font */
#define IS_STIK_FONT(p)    ( ((p->stik_font) >= (UW16)1) ? true : false )
#endif	/* STIK && (TT_DISK || TT_ROM || TT_ROM_ACT) */

#if CCC
/* for CCC font */
#define IS_CCC_FONT(p)    ( ((p->stik_font) == (UW16)2) ? true : false )
#endif

/* for CCC format */
/* changes for v7 CCC  08-20-04 qwu */
/* #define STIK_FORMAT_CCC (UW16)(0x0200) */
#define STIK_FORMAT_CCC(a)  ( ( a >= (UW16)(0x0200) ) &&  ( a <= (UW16)(0x02FF) ) )

#define CFFNAMELEN 65

#if FCO_RDR
#define MT_MISSING_GLYF_UNICODE		0xF491
#endif

#define IS_EFM_FONT(fc)    ( (fc)->font_hdr && (!STRCMP( (FILECHAR *)(((fc)->font_hdr) + PCL_CHR_HDR_SIZE), "EXTERNAL FONT MODULE")) )
/*-----------------------------------*/
/*       F O N T C O N T E X T       */
/*-----------------------------------*/
#if !SCALE_MATRIX    /* typographer version only */
typedef struct
{
    SL32    font_id;      /* font number                                */
#if CFF_RDR
    FILECHAR nameInSet[CFFNAMELEN]; /* added by dlk for CFF. Changed 20-Jul-98 awr */
#endif /* CFF_RDR */
#if PST1_RDR || (TT_RDR && CFF_RDR)	/* 01-22-02 jfd */
	/* if this value is set to 0, the field will be ignored; if this value
	is positive, it will replace the hardcoded PS_THRESHOLD value in comp_pix.c
	(which determines when missing-pixel recovery is in effect). */
	UL32	optionalThreshold;
#endif
    SW16    point_size;   /* point size of this character in 1/8th pts  */
    SW16    set_size;     /* set   size of this character in 1/8th pts  */
    CGPOINTFX shear;      /* cosine & sine of vertical stroke angle     */
    CGPOINTFX rotate;     /* cosine & sine of baseline rotation         */
    UL32   xres;          /* output pixels per meter                    */
    UL32   yres;
    CGFIXED xspot;
    CGFIXED yspot;
#if BOLD || BOLD_FCO
    SW16    xbold;
    SW16    ybold;
#endif
#if BOLD_HORIZONTAL
    UW16  bold_horizontal;
#endif
#if BOLD_VERTICAL
    UW16  bold_vertical;
#endif
#if BOLD_P6 
    UW16   pcl6bold;
#endif
    UW16   ssnum;
    UW16   format;       /* format flags                               */
    LPUB8  font_hdr;     /* pointer to font header */

#if BANDING
    SW16   band_top;     /* height of the band in pixels ( >= 0 )       */
    SW16   band_bot;     /* distance in pixels from the baseline to the */
#endif /* BANDING */

#if TT_RDR
    UL32   dl_ssnum;     /* download symbol set information:
                          * Bits 0-3:    Platform ID (if TT),
                          * Bits 4-7:    Specific ID (if TT),
                          * Bits 8-31:   Reserved,
                          */
#endif  /* TT_RDR */

#if TT_RDR	/* 11-13-00 jfd */
	UW16   user_platID;   /* user-supplied platform ID of CMAP table */
	UW16   user_specID;   /* user-supplied specific ID of CMAP table */
#endif	/* TT_RDR */

#if GRAYSCALING
    SW16   numXsubpixels;  /* number of subpixels along side of gray pixel */
                           /* values may be either 1, 2, 4, 8 and 16 */
    SW16   numYsubpixels;  /* number of sub-rasters deep of a gray pixel   */
    SW16   alignment;      /* grid alignment options: GAGG, GAGH, GAGP,
                            *    GAHG, GAHH, GAHP, GAPG, GAPH, GAPP
                            */
    SW16   numXphases;
    SW16   numYphases; 
#endif

    UL32   ExtndFlags;

#if SPECIAL_EFFECTS
	UW16	EffectsFlags;		/*	Bits 0-3:		Effect to be applied
								*	Bits 4 - 15:	Not used
								*/
#endif /* special_effects */
      
#if TT_RDR
	SW16	ttc_index;	/* index of font within TTC (TrueTypeCollection) file. */
#endif	
#if (T1MMASTER)
    SW16 userDesignVector[4];  /* Multi Master user design vector */
#endif
#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)	/* 12-06-01 jfd */
	CGFIXED stroke_pct;
#endif	/* STIK && (TT_DISK || TT_ROM || TT_ROM_ACT) */
#if ARABIC_AUTO_HINT
	BOOLEAN	arabic_auto_hint;
#endif
#if FS_DIRECT
	BOOLEAN	fs_direct_draw; /* if set by the application, allows for direct draw processing */
#endif
}  FONTCONTEXT;

#else                /* typographer and all matrix versions */

/* The following 4 typedefs are different ways of specifying the scaling
   parameters. They are all combined in a union in the FONTCONTEXT
   structure defined after.
*/

#if (SCALE_MATRIX & TYPO_SCALE_MATRIX)
typedef struct
{
    SW16    point_size;   /* point size of this character in 1/8th pts  */
    SW16    set_size;     /* set   size of this character in 1/8th pts  */
    CGPOINTFX shear;        /* cosine & sine of vertical stroke angle     */
    CGPOINTFX rotate;       /* cosine & sine of baseline rotation         */
    UL32   xres;         /* output pixels per meter                    */
    UL32   yres;
}  TYPO;
#endif


#if (SCALE_MATRIX & MAT0_SCALE_MATRIX)
/*  Design space to output space */

typedef struct
{
    SL32    m[4];         /* Design space to output space               */
    SW16    matrix_scale; /* number of fractional bits in m[] values    */
} MAT0;
#endif



#if (SCALE_MATRIX & MAT1_SCALE_MATRIX)
/* world to output and em box */

typedef struct
{
    SL32    m[4];         /* matrix from world space to output space    */
    SL32    em_width;     /* size of em box in world units              */
    SL32    em_depth;
    SW16    matrix_scale; /* number of fractional bits in m[] values    */
    SW16    em_scale;     /*   "     "     "        "   " em sizes      */
} MAT1;
#endif



#if (SCALE_MATRIX & MAT2_SCALE_MATRIX)
/* world to output and world res and pt and set */

typedef struct
{
    SL32    m[4];         /* matrix from world space to output space    */
    SL32    xworld_res;   /* world space resolution */
    SL32    yworld_res;
    SW16    matrix_scale; /* number of fractional bits in m[] values    */
    SW16    world_scale;  /*   "     "     "        "   " resolution    */
    SW16    point_size;
    SW16    set_size;
} MAT2;
#endif


/* FONTCONTEXT structure */

typedef struct
{
    SL32    font_id;      /* font number                                */
#if CFF_RDR
    FILECHAR nameInSet[CFFNAMELEN]; /* added by dlk for CFF. Changed 20-Jul-98 awr */
#endif /* CFF_RDR */
#if PST1_RDR || (TT_RDR && CFF_RDR)	/* 01-22-02 jfd */
	/* if this value is set to 0, the field will be ignored; if this value
	is positive, it will replace the hardcoded PS_THRESHOLD value in comp_pix.c
	(which determines when missing-pixel recovery is in effect). */
	UL32	optionalThreshold;
#endif
    CGFIXED xspot;
    CGFIXED yspot;
#if BOLD || BOLD_FCO
    SW16    xbold;
    SW16    ybold;
#endif
#if BOLD_HORIZONTAL
    UW16  bold_horizontal;
#endif
#if BOLD_VERTICAL
    UW16  bold_vertical;
#endif
#if BOLD_P6 
    UW16   pcl6bold;
#endif
    UW16   ssnum;
    UW16   format;       /* format flags                               */
    LPUB8  font_hdr;     /* pointer to font header */

#if BANDING
    SW16   band_top;     /* height of the band in pixels ( >= 0 )       */
    SW16   band_bot;     /* distance in pixels from the baseline to the */
#endif /* BANDING */

#if TT_RDR
    UL32   dl_ssnum;     /* download symbol set information:
                          * Bits 0-3:    Platform ID (if TT),
                          * Bits 4-7:    Specific ID (if TT),
                          * Bits 8-31:   Reserved,
                          */
#endif  /* TT_RDR */

#if TT_RDR	/* 11-13-00 jfd */
	UW16   user_platID;   /* user-supplied platform ID of CMAP table */
	UW16   user_specID;   /* user-supplied specific ID of CMAP table */
#endif	/* TT_RDR */

#if GRAYSCALING
    SW16   numXsubpixels;  /* number of subpixels along side of gray pixel */
                           /* values may be either  4, 8 */
    SW16   numYsubpixels;  /* number of sub-rasters deep of a gray pixel   */
    SW16   alignment;      /* grid alignment options: GAGG, GAGH, GAGP,
                            *    GAHG, GAHH, GAHP, GAPG, GAPH, GAPP
                            */
    SW16   numXphases;
    SW16   numYphases; 
#endif

    UL32   ExtndFlags; 
	
#if SPECIAL_EFFECTS
	UW16	EffectsFlags;		/*	Bits 0-3:		Effect to be applied
								*	Bits 4 - 15:	Not used
								*/
#endif /* special_effects */

#if TT_RDR
	SW16	ttc_index;	/* index of font within TTC (TrueTypeCollection) file.
							Index is ignored if not using a TTC file. */			
#endif	
#if (T1MMASTER)
    SW16 userDesignVector[4];  /* Multi Master user design vector */
#endif

#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)	/* 12-06-01 jfd */
	CGFIXED stroke_pct;
#endif	/* STIK && (TT_DISK || TT_ROM || TT_ROM_ACT) */

    UW16  fc_type;     /* selector indicates which union type */
    union
    {
#if (SCALE_MATRIX & TYPO_SCALE_MATRIX)
        TYPO  t;
#endif
#if (SCALE_MATRIX & MAT0_SCALE_MATRIX)
        MAT0  m0;
#endif
#if (SCALE_MATRIX & MAT1_SCALE_MATRIX)
        MAT1  m1;
#endif
#if (SCALE_MATRIX & MAT2_SCALE_MATRIX)
        MAT2  m2;    
#endif
    } s;
#if ARABIC_AUTO_HINT
BOOLEAN arabic_auto_hint;	
#endif
#if FS_DIRECT
	BOOLEAN	fs_direct_draw; /* if set by the application, allows for direct draw processing */
#endif
} FONTCONTEXT;

/* values for fc_type */

#define FC_TYPO_TYPE      0   /* Typographer                        */
#define FC_MAT0_TYPE      1   /* Matrix design unit to output       */
#define FC_MAT1_TYPE      2   /*        world to output   em box    */
#define FC_MAT2_TYPE      3   /*                          pt, set   */

#endif  /*  SCALE_MATRIX not 0 */


typedef FONTCONTEXT FARPTR * PFONTCONTEXT;

#ifdef _WINDOWS
#define UFST_MAXPATHLEN        128 /* max length of pathname (incl null) */
#else
#define UFST_MAXPATHLEN        65  /* max length of pathname (incl null) */
#endif /* _WINDOWS */


/*----------------------------------*/
/*    Character Bitmap              */
/*----------------------------------*/

/*----------------------*/
/*  Doubly Linked Lists */
/*----------------------*/
typedef struct
{ 
    MEM_HANDLE  f;  /* forward pointer */
    MEM_HANDLE  b;  /* backward pointer */
} DLL;
typedef DLL FARPTR * PDLL;

typedef struct
{
    DLL     link;          /* doubly linked list pointers                  */
    UW16   notmru;        /* set to 0 if recently made most recently used */
    HIFFONT hiffont;       /* Font handle of the owner of this IFBITMAP    */
    SL32    index;         /* index to IFBITMAP in above FONT's table      */   /* rjl 9/20/2002 - changed from SW16*/
#if CACHE && CACHE_BY_REF
	UL32	ref_counter;	/* "cache-by-reference" count */
#endif
#if CACHE && USE_ASIAN_CACHE
    MEM_HANDLE  f;         /* forward ptr to next IFBITMAP         */
    MEM_HANDLE  b;         /* backward ptr to next IFBITMAP        */
#endif

    SW16    width;         /* bit map width (bytes)                 */
    SW16    depth;         /*  "   "  depth (pixels)                */
    SW16    left_indent;
    SW16    top_indent;
    SW16    black_width;
    SW16    black_depth;
    SL32    xorigin;       /*  (1/16th pixel)  */
    SL32    yorigin;       /*  (1/16th pixel)  */
    SW16    escapement;    /* character body width (design units)  */
                           /*  native units if DU_ESCAPEMENT == 1, */
                           /*  else 8782's                         */
    SW16    du_emx;        /* x-size of em box                     */
    SW16    du_emy;        /* y-size of em box                     */

#if TT_SCREENRES
	SW16	pixelWidth;/* "devAdvWidth", shifted to 1/16th pixel    */
			   /* however, this should be INTEGRAL # pixels */

    CGPOINTFX advanceWidth;/* Scaled escapement, 16.16. jwd, 08/18/02   */
#endif

#if EMBEDDED_BITMAPS && TT_RDR && (CGBITMAP	|| GRAYSCALING) /* Added rjl 7/22/2003 - */
	BOOLEAN usedEmbeddedBitmap;	/* TRUE if an embedded bitmap was used for char*/
#endif /* EMBEDDED_BITMAPS && TT_RDR && (CGBITMAP || GRAYSCALING) */

#if GRAYSCALING || COMPRESSED_CACHE
    SL32    size;          /* Size in bytes of everythg after this */
#endif

#if GET_VERTICAL_METRICS 
	SW16 topSideBearing;
	UW16 advanceHeight;
#endif

#if !IFBITSPLIT		/* regular case - IFBITMAP header & data in one structure */

/*** bm[] MUST be the LAST element in the IFBITMAP structure - if you need
	to add new elements to the structure, put them before bm[] ***/
/*** this is because of the use of the BMHEADERSIZE definition, below ***/

    UL32   bm[1];          /* character bit map                    */

#else	/* special case - IFBITMAP header & data in two allocated structures */

	UL32 *bm;
    MEM_HANDLE datahandle;
#endif	/* !IFBITSPLIT */ 	 

} IFBITMAP;

typedef IFBITMAP FARPTR * PIFBITMAP;
typedef IFBITMAP FARPTR * FARPTR * PPIFBITMAP;

#if !IFBITSPLIT		/* regular case */
#define BMHEADERSIZE   (UL32)(sizeof(IFBITMAP) - 4) /* IFBITMAP header size  */
#define CHAR_SIZE_ARRAYSIZE 3
#define JUST_HEADER_IDX		0

#else				/* special case */
#define BMHEADERSIZE   (UL32)(sizeof(IFBITMAP)) /* IFBITMAP header size  */
#define CHAR_SIZE_ARRAYSIZE 4
#define JUST_HEADER_IDX		3
#endif	/* !IFBITSPLIT */

#define NZW_SIZE_IDX		1
#define CCBUF_SIZE_IDX		2 	 

typedef struct {
    UW16 num_segmts;
    UW16 num_coords;
    UW16 polarity;
    UW16 segmt_offset;
    UW16 coord_offset;
} OUTLINE_LOOP;
typedef OUTLINE_LOOP FARPTR * POUTLINE_LOOP;

typedef struct {
  SL32  size;
  SW16  pad;
  UW16 num_loops;
  OUTLINE_LOOP loop[1];  /* actual # of loops depends on character */
  /* Actual outline data follows this array. */
} OUTLINE_CHAR;
typedef OUTLINE_CHAR FARPTR * POUTLINE_CHAR;

typedef struct
{
    DLL     link;          /* doubly linked list pointers                  */
    UW16   notmru;        /* set to 0 if recently made most recently used */
    HIFFONT hiffont;       /* Font handle of the owner of this IFBITMAP    */
    SL32    index;         /* index to IFBITMAP in above FONT's table      */   /* rjl 9/20/2002 - was SW16 */
#if CACHE && CACHE_BY_REF
	UL32 ref_counter;      /* "cache-by-reference" count */
#endif
#if ASIAN_ENCODING && CACHE
    MEM_HANDLE  f;         /* forward ptr to next IFOUTLINE        */
    MEM_HANDLE  b;         /* backward ptr to next IFOUTLINE       */
#endif

    SL32     size;         /* size of header w/out metrics */
    SW16    depth;         /*  always 1                */
    INTR    left;
    INTR    top;
    INTR    right;
    INTR    bottom;
#if VLCOUTPUT
    SW16    VLCpower;
#else
    SW16    xscale;
    SW16    yscale;
#endif
    SW16    escapement;    /* character body width (design units)  */
                           /*  native units if DU_ESCAPEMENT == 1, */
                           /*  else 8782's                         */
    SW16    du_emx;        /* x-size of em box                     */
    SW16    du_emy;        /* y-size of em box                     */

#if TT_SCREENRES
                           /* jwd, 07/24/02.                            */
	SW16	pixelWidth;/* "devAdvWidth", shifted to 1/16th pixel    */
                           /* however, this should be INTEGRAL # pixels */
    CGPOINTFX advanceWidth;/* Scaled escapement, 16.16. jwd, 08/18/02   */
#endif

#if GET_VERTICAL_METRICS 
	SW16 topSideBearing;
	UW16 advanceHeight;
#endif

/*** ol MUST be the LAST element in the IFOUTLINE structure - if you need
	to add new elements to the structure, put them before bm[] ***/
/*** this is because of the use of the OLHEADERSIZE definition, below ***/

    OUTLINE_CHAR    ol;    /* changed from 3.01 to match actual data */

} IFOUTLINE;

typedef IFOUTLINE FARPTR * PIFOUTLINE;
typedef IFOUTLINE FARPTR * FARPTR * PPIFOUTLINE;

/* IFOUTLINE header size */
#define OLHEADERSIZE   (sizeof(IFOUTLINE) - sizeof(OUTLINE_CHAR))

/*----------------------------------*/
/*    Tile structures               */
/*----------------------------------*/
typedef struct
{
    SW16 x;      /* coordinate of upper left corner of tile  */
    SW16 y;
    SW16 width;  /* dimensions of tile */
    SW16 depth;

}  IFTILE;
typedef IFTILE FARPTR * PIFTILE;


#define FNT_MET_NAM_LEN  50

#define IFFAMILYNAME_LENGTH   20    /* size taken from FAIS data specification */
#define IFTYPEFACENAME_LENGTH 50    /* size taken from FAIS data specification */

/*----------------------------------*/
/*    Font Metrics Structure        */
/*----------------------------------*/
typedef struct
{
    UW16 unitsPerEm;
    SW16 fontAscent;
    SW16 fontDescent;
    SW16 maxAscent;
    SW16 maxDescent;
    UW16 leading;
	UW16 embeddingBits;
    UB8  styleBold;       /* 0=regular; 1=bold      */
    UB8  styleItalic;     /* 0=upright; 1=italic    */
    UB8  styleSerif;      /* 0=serif;   1=sansserif */
    UB8  styleFixedPitch; /* 0=proportional; 1=fixedpitch */
    SB8  tfName[FNT_MET_NAM_LEN];
    SB8  famName[FNT_MET_NAM_LEN];
}  FONT_METRICS;


/*-----------------------------------*/
/*    Width List Entry Structures    */
/*-----------------------------------*/

typedef struct   /* jwd, 10/15/02 */
   {
   union
      {
      UL32  TT_unicode;
      UL32  MT_unicode;
      UW16  IF_cgnum;
      VOID *PS_name;
      } CharType;
   }  WIDTH_LIST_INPUT_ENTRY;
typedef WIDTH_LIST_INPUT_ENTRY FARPTR *PWIDTH_LIST_INPUT_ENTRY;

typedef struct   /* jwd, 10/15/02 */
   {
   UW16  CharWidth;
   UW16  ScaleFactor;
   }  WIDTH_LIST_OUTPUT_ENTRY;
typedef WIDTH_LIST_OUTPUT_ENTRY FARPTR *PWIDTH_LIST_OUTPUT_ENTRY;

typedef struct
   {
   MEM_HANDLE DLBitmapPtr;
   } DLBITMAPPTR;

typedef DLBITMAPPTR FARPTR * PDLBITMAPPTR;
typedef DLBITMAPPTR FARPTR * FARPTR * PPDLBITMAPPTR;

#define MAKbitMap  CGIFmakechar

/**********************************************************/
/*                                                        */
/*                         CG macros                      */
/*                                                        */
/**********************************************************/

#if FONTBBOX
#define CGIFP_MAX_BADCHARS    2
#define CGIFP_IF_SCALABLE    10
#endif

#define CGIFcache_purge( a )  CGIFfont_purge( (a) )


/* Structure used to return UFST version information to the user -
	current values for the UFST-version fields are defined in cgif.c,
	just above the API call CGIFwhat_version()
*/

#define PATCHSTRING_LEN 30
#define UFST_VERSION_MAJOR 5
#define UFST_VERSION_MINOR 0

#define ufst_version_patchstring "1"

typedef struct
{
	UW16 major_version;
	UW16 minor_version;
	FILECHAR patchstring[PATCHSTRING_LEN];	
} 
UFST_VERSION_INFO;

/* CGIFbucket_purge() constants. jwd, 11/18/02. */
#define DELETEALL          1
#define DELETEALLBUTLOCKED 2

/* CGIFmem_purge() constants.*/
#define CACHEPURGE        1
#define BUFFERPURGE	      2
#define CACHEBUFFERPURGE  3

/* constants for stroke font processsing */
#define CJK			      0
#define LATIN             1	
#define ARABIC_SCRIPT	  2

#endif	/* __CGIF__	*/

