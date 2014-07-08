
/* Copyright (C) 2013 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* cgif.h */


#ifndef __CGIF__
#define __CGIF__

typedef MEM_HANDLE   HIFFONT;
typedef MEM_HANDLE   HIFBITMAP;
typedef MEM_HANDLE   HIFOUTLINE;
typedef HIFBITMAP * PHIFBITMAP;
typedef HIFOUTLINE * PHIFOUTLINE;

/* CGIFbucket_purge() constants. */
#define DELETEALL          1
#define DELETEALLBUTLOCKED 2

/* CGIFmem_purge() constants. */
#define CACHEPURGE        1
#define BUFFERPURGE	      2
#define CACHEBUFFERPURGE  3

/* Bits in CGIFinitstate indicating components not initialized */
#define not_buf   0x0002
#define not_cac   0x0004
#define not_enter 0x0008
#define not_intellifont 0x0010

/* Bits in MTinitstate indicating components not initialized */
#define not_BUCKlru 0x0002
#define not_FNTlru  0x0004
#define not_BMlru   0x0008

/* possible values of "extern_font" fields */

#define NOT_A_DOWNLOAD 0			/* rom or disk-based font */
#define PCL_GENERIC_DOWNLOAD 1		/* IF, PS, or TT Format 15 PCL download */
#define PCL_TTFMT16_DOWNLOAD 2		/* TT Format 16 PCL download */
#define PCL_TTXL_DOWNLOAD 3			/* TT XL Format PCL download */
#define PCL_BITMAP_DOWNLOAD 254		/* PCL bitmap download */

/* IF PCLEO defines */
#if FONTBBOX
#define CGIFP_MAX_BADCHARS    2
#define CGIFP_IF_SCALABLE    10
#endif

#if FCO_RDR
#define MT_MISSING_GLYF_UNICODE		0xF491
#endif

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

#define  ERR_nz_contours_wrong 14  /* Some contours are inconsistant for psuedo bold */
#define  ERR_odd_transitions   15  /* odd number of transitions in a raster line */

#define  ERR_bad_pool          21
#define  ERR_pool_too_large    22
#define  ERR_CACinit           32
#define  ERR_IXinit            33
#define  ERRlong_font_name     51
#define  ERR_fst_type          52  /* invalid fst type in fc->format */
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
#define  ERR_obsolete_stroke   96  /* the stroke font is not offlined */
#define  ERR_obsolete_CCC	   98  /* CCC format no longer supported */

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
#define ERR_JOHABTOUNI_READ			139 
#define ERR_UNITOJOHAB_READ         140 

#define  ERR_if_read_libfile 150    /* dynamic fonts, read error */
#define  ERR_if_process_face 151    /* dynamic fonts, invalid libr format */

#define  ERR_char_not_cached       160	/* cache-by-reference, char not cached  */
#define  ERR_ref_counter_overflow  161	/* cache-by-reference, > MAX_ULONG references */


/***********************************************************************/
/* Error codes 200-299 reserved for pcleomgt (PCL download) error codes:
	see pcleomgt.h for a complete list of these codes */
/***********************************************************************/	

/*
PostScript Input error messages (300 - 379, plus 1200 - 1399)
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
#define ERR_psi_CidSubrVals        363  /* error getting buf data inside get_cid_subr_vals */
#define ERR_psi_no_moveto          376 /* a path does not start with a moveto */

/* Type 1 or CFF stack errors encountered (when PS_ERRORCHECK option enabled) */
#define ERR_psi_stack_underflow	377		/* attempting to pop more than you've pushed */
										/* (probably signalling a corrupted font) */
#define ERR_psi_stack_overflow	378		/* attempting to push too much data onto the stack */
										/* (probably signalling a corrupted font) */
										
/*
More MM and CFF codes are located in the 1200 - 1399 range
*/
										
#define ERR_PCL_NAME_NOT_FOUND 380 /* EFM font does not contain font name specfied */
#define ERR_PCL_NOT_TT_HEADER  381 /* not a TT PCL download */
#define ERR_PCL_BAD_TT_HEADER  382 /* PCL TT download header not valid */

#define ERR_TT_OPENDISK      400 /* load_font: can't open disk font file. */
#define ERR_TT_BADCONFIG     402 /* load_font: attempt to load font type not enabled in cgconfig.h */
#define ERR_TT_MEMFAIL       403 /* load_font: not enough buffer memory. */
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

#define ERR_TTEMB_INVIMAGFORMAT		420	/* invalid image format for embedded bitmap */
#define ERR_TTEMB_IMAGFORMATNOTSUPP	421	/* embedded bitmap image format 3 or 4 not supported */
#define ERR_TTEMB_MISSINGGLYPH		422	/* embedded bitmap composite has missing glyph */

#define ERR_stroke_pct       423
#define ERR_stik_extract_outline    424
#define ERR_reflines_needed  426

#define ERR_TT_UNSUPPORTED_FONT_TYPE   427	/* TT font (or variant) is not supported by UFST */
#define ERR_TT_UNSUPPORTED_FUNCTION   428	/* function not supported for config */
/* CGIFtt_cmap_query() error */
#define ERR_TT_TOO_MANY_CMAPS		433 /* too many cmap subtables to fit into
										preallocated query structure - this is
										a non-fatal error, because some cmap
										info will still be returned */

#define	ERR_TT_NO_GLYF_TABLE		440

/* Font validation errors */
#define ERR_FNT_INNEREXECUTE				441	/* error returned by fnt_InnerExecute() */
#define ERR_TT_FONTVAL_NUMTABLES			450	/* unreasonable number of tables (==0 || > 50) in Table Directory */
#define ERR_TT_FONTVAL_TABLEDIR_OFFSETS		451	/* overlapping of tables (offsets/lengths) in Table Directory */
#define ERR_TT_FONTVAL_MISSING_REQ_TABLE	452	/* missing required table in Table Directory */
#define ERR_TT_FONTVAL_FPGM_STACK_OVERFLOW  454 /* number of items pushed onto FPGM stack exceeds MAX */
#define ERR_TT_FPGM_STACK_UNDERFLOW  455 		/* number of items pushed onto FPGM stack exceeds MAX */

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
#define ERR_fco_FontNew_BadFontIndex 540
#define ERR_fco_XYdata_too_small	 550


#define  ERR_bm_gt_oron       599  /* Char bitmap too big for on array */
#define  ERR_bm_too_big       600  /* Char bitmap too big */
#define  ERR_mem_char_buf     602  /* Can't malloc character buffer */
#define  ERRmatrix_range      603  /* scaling outside of range */
#define  ERRdu_pix_range      604  /* scaling outside of range */
#define  ERR_if_init_glob     605  /* if_init_glob() failed */
#define  ERR_comp_pix         606  /* com_pix() failed */
#define  ERR_fixed_space      607  /* character is a fixed space */
#define  ERRsingular_matrix   608  /* scaling matrix has 0 determinate */
#define  ERRDLBmpInProcess    609  /* Processing PCL DL bitmaps */
#define  ERR_skeletal_init    610
#define  ERR_y_skel_proc      611
#define  ERR_x_skel_proc      612
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
#define  ERRgray_trans_too_small	 964
#define  ERRnotenoughprecision       965
#define  ERRgray_if_not_supported    966  /* No Intellifont */
#define  ERRchar_size_not_supported  967  /* CGIFchar_size() not supported */
#define  ERRgray_BytestreamNoMem     968
#define  ERRgray_nzw_too_small		 969

/* Banding */
#define  ERRbanding_not_supported    970  /* Banding not supported with  */
                                          /* SCALE_MATRIX != 0           */                                                                            
/* Floating-point */
#define ERR_FPdivide_by_zero     975
                                         

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
#define ERR_cff_bad_encode      1224  /* bad encoding file position        */
#define ERR_cff_bad_str_pos     1225  /* bad charstrings file position     */
#define ERR_cff_bad_str_mem     1226  /* no charstrings memory             */
#define ERR_cff_Chamel_notsupp  1228  /* Chameleon fonts not supported     */
#define ERR_cff_get_string_data 1229  /* Get text or data string failed    */
#define ERR_cff_bad_charset     1230  /* Charset read failure in Top DICT  */
#define ERR_cff_charset_arr_mem 1233  /* Charset mem not properly set up   */
#define ERR_cff_cg2idx          1235  /* Unable to get index for cgnum     */
#define ERR_cff_chname2SID      1236  /* Unable to convert PSchname to SID */
#define ERR_cff_idx2SID         1238  /* Unable to get SID for mapidx      */
#define ERR_cff_chId2cgnum      1239  /* Unable to get cgnum for chId in SS*/
#define ERR_cff_FDSelect_mem 	1240  /* FDSelect mem not properly set up  */
#define ERR_cff_FDSelect_fmt	1241  /* FDSelect format not supported     */



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

/*
Codes 1401 - 1419 Reserved For Type 42 Font Errors; from FONT_MGT.C
*/
#define TYPE42Err_OPEN             1401   /* error on file open            */
#define TYPE42Err_HDR_READ         1402   /* unable to read header         */
#define TYPE42Err_HDR_ENTRY_ALLOC  1403   /* unable to alloc header entry  */
#define TYPE42Err_HDR_REALLOC      1404   /* bad realloc                   */

/* Font Validation error codes */

/* non  errors */
#define FILECHECKSUMERROR	1501
#define CHECKSUMERRORHEAD	1502
#define CHECKSUMERRORHHEA	1503
#define CHECKSUMERRORMAXP	1504
#define CHECKSUMERROROS2	1505
#define CHECKSUMERRORNAME	1506
#define CHECKSUMERRORPOST	1507
#define CHECKSUMERRORHMTX	1508
#define CHECKSUMERRORCMAP	1509
#define CHECKSUMERRORPREP	1510
#define CHECKSUMERRORFPGM	1511
#define CHECKSUMERRORCVT	1512
#define CHECKSUMERRORVORG	1513
#define CHECKSUMERRORCFF	1514
#define CHECKSUMERROREBLC	1515
#define CHECKSUMERROREBDT	1516
#define CHECKSUMERROREBSC	1517
#define CHECKSUMERRORBASE	1518
#define CHECKSUMERRORGDEF	1519
#define CHECKSUMERRORGSUB	1520
#define CHECKSUMERRORGPOS	1521
#define CHECKSUMERRORJSTF	1522	
#define CHECKSUMERRORLOCA	1523
#define CHECKSUMERRORGLYF	1524	

/*  errors */
#define ERRORINFILEHEADER	1525
#define ERRORINTABLEHEAD	1526
#define ERRORINTABLEHHEA	1527
#define ERRORINTABLEMAXP	1528
#define ERRORINTABLEOS2		1529
#define ERRORINTABLENAME	1530
#define ERRORINTABLEPOST	1531
#define ERRORINTABLEHMTX	1532
#define ERRORINTABLECMAP	1533
#define ERRORINTABLEPREP	1534
#define ERRORINTABLEFPGM	1535
#define ERRORINTABLECVT		1536
#define ERRORINTABLEVORG	1537
#define ERRORINTABLECFF		1538
#define ERRORINTABLEEBLC	1539
#define ERRORINTABLEEBDT	1540
#define ERRORINTABLEEBSC	1541
#define ERRORINTABLEBASE	1542
#define ERRORINTABLEGDEF	1543
#define ERRORINTABLEGSUB	1544
#define ERRORINTABLEGPOS	1545
#define ERRORINTABLEJSTF	1546
#define ERRORINTABLELOCA	1547
#define ERRORINTABLEGLYF	1548
#define TABLENOTFOUNDHEAD	1549
#define TABLENOTFOUNDHHEA	1550
#define TABLENOTFOUNDMAXP	1551
#define TABLENOTFOUNDOS2	1552
#define TABLENOTFOUNDNAME	1553
#define TABLENOTFOUNDPOST	1554
#define TABLENOTFOUNDHMTX	1555
#define TABLENOTFOUNDCMAP	1556
#define TABLENOTFOUNDVORG	1557
#define TABLENOTFOUNDCFF	1558
#define TABLENOTFOUNDJSTF	1559	
#define TABLENOTFOUNDLOCA	1560
#define TABLENOTFOUNDGLYF	1561
#define TABLENOTFOUNDVHEA	1562

/* end Font Validation error codes */

/* Extended TT error checking error codes */
#define ERR_TT_NULL_FUNCDEF				1581
#define ERR_TT_NULL_PGMINDEX			1582
#define ERR_TT_CVT_OUT_OF_RANGE			1583
#define ERR_TT_OPCODE_OUT_OF_RANGE		1584
#define ERR_TT_COORDINATE_OUT_OF_RANGE	1585
#define ERR_TT_FDEF_OUT_OF_RANGE		1586
#define ERR_TT_PROGRAM_OUT_OF_RANGE		1587 
#define ERR_TT_ELEMENT_OUT_OF_RANGE		1588
#define ERR_TT_STORAGE_OUT_OF_RANGE		1589
#define ERR_TT_CONTOUR_OUT_OF_RANGE		1590
#define ERR_TT_POINT_OUT_OF_RANGE		1591
#define ERR_TT_ELEMENTPTR_OUT_OF_RANGE	1592
#define ERR_TT_STACK_OUT_OF_RANGE		1593

/* linked-font error codes */
#define ERR_too_many_fonts				2020	/* too many linked fonts */

/* Codes 2030 - 2049: mutex error codes */
#define ERR_BAD_REF_COUNT				2030
#define ERR_MUTEX_CREATE				2031
#define ERR_MUTEX_GONE					2032
#define ERR_MUTEX_TIMEOUT				2033

#define ERR_creating_mutex				2040
#define ERR_obtaining_mutex				2041
#define ERR_releasing_mutex				2042
#define ERR_deleting_mutex				2043

/* Codes 2050 - 2059: multithread error codes */
#define ERR_max_threads_exceeded		2050
#define ERR_thread_not_found_in_list	2051

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
#define ERR_TT_INSTRUCTION_DATA				3015
#define ERR_TT_COMPONENT_DATA				3016
**************************************************/


/* Error codes 4000 - 4999 reserved for EDGE-specific errors */
#define ERR_TABLE_UNSUPPORTED       4441
#define ERR_SCALE_LIMIT             4502
#define ERR_SCALE_DEGENERATE        4503
#define ERR_make_ADF_graymap        4504
#define ERR_adf_mem                 4505


/* error code 5000 - 5099 reserved for type checking 
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


/* don't change this 0x7fff value, since the code is returned from get-width functions,
	and its value must be easily distinguished from an escapement value */
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
    LPUB8 cc_buf_ptr;             /* compound character buffer pointer */
    SL32  cc_buf_size;            /*    "        "        "     size   */
#endif
} IFCONFIG;
typedef IFCONFIG * PIFCONFIG;



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

        Bit 5  (0x0020)  Unused

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
*/


/**********************************************/
/*** defines for bits in FONTCONTEXT.format ***/
/**********************************************/

/* 0x3c20 = 0011,1100,0010,0000, bits with 1 are those taken from the member font format */
/* 0xc3df = 1100,0011,1101,1111 = ~0x3c20, 1 bits are those not used by user for format */
#define  BITS_USED_IN_FORMAT	0x3c20	/* used for LINKED_FONT */

#define FC_OUTPUT_MASK    0x000F   /* output type bits w/in format field */
#define FC_BITMAP_TYPE    0x0000   /* bitmap                             */
#define FC_LINEAR_TYPE    0x0001   /* linear outline                     */
#define FC_QUAD_TYPE      0x0002   /* quadratic outline                  */
#define FC_CUBIC_TYPE     0x0003   /* cubic outline                      */
#define FC_GRAY_TYPE      0x0005   /* graymap align to subpixels         */

#define FC_NON_Z_WIND     0x0010   /* non-zero winding scanning method   */
/* one unused bit here = 0x0020 */
#define FC_PCL6_EMU       0x0040
#define FC_INCHES_TYPE    0x0080   /* Output resolution is in pixels/inch */

#define FC_PSUNITS_MASK   0x0300   /* Pt sz units bits w/in format fld   */
#define FC_8THS_TYPE      0x0000   /* Pt sz in 8ths (default)            */
#define FC_16THS_TYPE     0x0100   /* Pt sz in 16ths                     */
#define FC_20THS_TYPE     0x0200   /* Pt sz in 20ths   AOF Feb. 11, 2000 */

#define FC_EXTERN_TYPE    0x0400   /* PCLEO file format                  */
#define FC_ROM_TYPE       0x0800   /* ROM based file                     */

#define FC_FONTTYPE_MASK  0x3000
#define FC_IF_TYPE        0x0000   /* Intellifont                        */
#define FC_PST1_TYPE      0x1000   /* PostScript Type 1 font             */
#define FC_TT_TYPE        0x2000   /* TrueType font                      */
#define FC_FCO_TYPE       0x3000   /* Font Collection Object */


/*************************************************/
/*** macros to test bits in FONTCONTEXT.format ***/
/*************************************************/

#define FC_ISROM(fc)    ( (fc)->format & FC_ROM_TYPE)
#define FC_ISEXTERN(fc) ( (fc)->format & FC_EXTERN_TYPE)

#define FC_ISIF(fc)    (( (fc)->format & FC_FONTTYPE_MASK ) == FC_IF_TYPE)
#define FC_ISPST1(fc)  (( (fc)->format & FC_FONTTYPE_MASK ) == FC_PST1_TYPE)
#define FC_ISTT(fc)    (( (fc)->format & FC_FONTTYPE_MASK ) == FC_TT_TYPE)
#define FC_ISFCO(fc)   (( (fc)->format & FC_FONTTYPE_MASK ) == FC_FCO_TYPE)

#define FC_IS8THS(fc)   (((fc)->format & FC_PSUNITS_MASK ) == FC_8THS_TYPE)
#define FC_IS16THS(fc)  (((fc)->format & FC_PSUNITS_MASK ) == FC_16THS_TYPE)
#define FC_IS20THS(fc)  (((fc)->format & FC_PSUNITS_MASK ) == FC_20THS_TYPE)


#define FC_ISNONZWIND(fc) ( (fc)->format & FC_NON_Z_WIND )
#define FC_ISPCL6EMU(fc)  ( (fc)->format & FC_PCL6_EMU )

#define FC_ISBITMAP(fc) (( (fc)->format & FC_OUTPUT_MASK ) == FC_BITMAP_TYPE)
#define FC_ISLINEAR(fc) (( (fc)->format & FC_OUTPUT_MASK ) == FC_LINEAR_TYPE)
#define FC_ISQUAD(fc)   (( (fc)->format & FC_OUTPUT_MASK ) == FC_QUAD_TYPE)
#define FC_ISCUBIC(fc)  (( (fc)->format & FC_OUTPUT_MASK ) == FC_CUBIC_TYPE)
#define FC_ISGRAY(fc)   (( (fc)->format & FC_OUTPUT_MASK ) == FC_GRAY_TYPE)

#define FC_ISOUTLINE(fc) ( (((fc)->format & FC_OUTPUT_MASK) == FC_LINEAR_TYPE )\
                        || (((fc)->format & FC_OUTPUT_MASK) == FC_QUAD_TYPE )\
                        || (((fc)->format & FC_OUTPUT_MASK) == FC_CUBIC_TYPE ) )

                        
/**************************************************/
/*** defines for bits in FONTCONTEXT.ExtndFlags ***/
/**************************************************/

/* 0xFF3B = 1111,1111,0011,1011, bits with 1 are those taken from the member font ExtndFlags  */
/* 0x00C4 = 0000,0000,1100,0100 = ~0xFF3B,  */
#define  BITS_USED_IN_ExtndFlags	0x0000FF3B	/* used for LINKED_FONT */

#define EF_FONTTYPE_MASK  0x1001403B    /* which ExtndFlags bits are set on a per-font basis? */
										/* (i.e. which bits should be cleared when loading a new font) */

/* currently-defined bits = EF_TYPE42FONT_TYPE, EF_FORMAT16_TYPE, EF_ACT_TYPE, EF_XLFONT_TYPE, EF_DIMM_DISK_TYPE, EF_EDGEFONT_TYPE, EF_CID_TYPE1, LINKED_FONT_TYPE */


#define EF_FORMAT16_TYPE  0x00000001    /* ExtndFlags mask, format 16 PCLETTO */
#define EF_ACT_TYPE       0x00000002    /* ExtndFlags mask, AGFA Compressed TrueType Object */
#define EF_HP4000_TYPE    0x00000004    /* ExtndFlags mask, HP4000 emulation. */
#define EF_XLFONT_TYPE    0x00000008    /* ExtndFlags mask, XL PCLETTO */
#define EF_DIMM_DISK_TYPE 0x00000010    /* Font is in special memory that uses disk i/o */

#define EF_EDGEFONT_TYPE  0x00000020    /* ExtndFlags mask, EDGE font */

#define EF_TT_NOPLUGS     		0x00000040  /* ExtndFlags bit to disable plugin support for STIK fonts */
											/* (compare with EF_GLOBAL_NOPLUGS, below) */
#define EF_SUBSTHOLLOWBOX_TYPE	0x00000080	/* ExtndFlags bit to substitute hollow box character for missing char */

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

#define LINKED_FONT_TYPE  0x00010000
#define EF_TT_SCREENRES	  0x00020000	/* ExtndFlags bit to enable special processing for TT at very small screen resolutions */

/*** >>> 2 more ExtndFlags bits available here <<< ***/

#define EF_GLOBAL_NOPLUGS	0x00100000	/* ExtndFlags bit to disable plugin support for all font types */
										/* (compare with EF_TT_NOPLUGS, above) */
#define EF_FORCE_MPR		0x00200000 /* New mechanism for forcing Missing Pixel Recovery */ 
#define EF_NOUSBOUNDBOX     0x00400000 /* ExtndFlags bit to DISABLE USBOUNDBOX option */
#define EF_XPS_SIDEWAYS		0x00800000 /* Enable computation of XPS vertical metrics */ 

#define EF_NOSYMSETMAP		0x01000000	/* New mechanism for triggering NO_SYMSET_MAPPING = set this bit */
#define EF_PARTIAL_GLYPH_OK 0x02000000 /* ExtndFlags bit to enable printing of glyphs with missing components*/
#define EF_TT_ERRORCHECK_ON 0x04000000 /* ExtndFlags bit to enable extended TT error checking */

/*** >>> 1 more ExtndFlags bit available here *<<< **/

#define EF_TYPE42FONT_TYPE  0x10000000  /* ExtndFlags mask, Type 42 Font */

#define EF_USE_VARIATION_SELECTOR 0x20000000	/* ExtndFlags mask, use variation selector */
/*** >>> 3 more ExtndFlags bits available here *<<< **/


/*****************************************************/
/*** macros to test bits in FONTCONTEXT.ExtndFlags ***/
/*****************************************************/

#define FC_ISTTFMT16(fc) ((fc)->ExtndFlags & EF_FORMAT16_TYPE)
#define FC_ISACT(fc)     ((fc)->ExtndFlags & EF_ACT_TYPE)
#define FC_ISHP4000(fc)  ((fc)->ExtndFlags & EF_HP4000_TYPE)
#define FC_ISXLFONT(fc)  ((fc)->ExtndFlags & EF_XLFONT_TYPE)
#define FC_ISTYPE42FONT(fc)  ((fc)->ExtndFlags & EF_TYPE42FONT_TYPE)
#define FC_ISDIMM_DISK(fc)  ((fc)->ExtndFlags & EF_DIMM_DISK_TYPE)

#define FC_ISVERTWRIT(fc)	((fc)->ExtndFlags & EF_VERTWRIT_MASK)
#define FC_ISUFSTVERT(fc) ((fc)->ExtndFlags & EF_UFSTVERT_TYPE )
#define FC_ISVERTSUB(fc) ((fc)->ExtndFlags & EF_VERTSUBS_TYPE )
#define FC_ISGALLEYSEG(fc) ((fc)->ExtndFlags & EF_GALLEYSEG_TYPE )
#define FC_ISVERTEXSEG(fc)  ((fc)->ExtndFlags & EF_VERTEXSEG_TYPE ) 

#define FC_ISCONTCHK(fc) ((fc)->ExtndFlags & EF_CONTCHK)
#define FC_DONTHINTTT(fc) ((fc)->ExtndFlags & EF_TT_NOHINT)
#define FC_FORCE_MPR(fc) ((fc)->ExtndFlags & EF_FORCE_MPR )
#define FC_NOUSBOUNDBOX(fc) ((fc)->ExtndFlags & EF_NOUSBOUNDBOX )

#define FC_ISXPS_SIDEWAYS(fc) ( ((fc)->ExtndFlags & EF_XPS_SIDEWAYS ) && \
								!FC_ISPST1((fc)) && \
								!FC_ISIF((fc)) )
								
#define FC_STIKNOPLUGINS(fc) ((fc)->ExtndFlags & EF_TT_NOPLUGS)
#define FC_NOPLUGINS(fc) ((fc)->ExtndFlags & EF_GLOBAL_NOPLUGS)
#define FC_NO_SSMAP(fc) ((fc)->ExtndFlags & EF_NOSYMSETMAP)
#define FC_SSMAP(fc) !((fc)->ExtndFlags & EF_NOSYMSETMAP)
#define FC_ISHOLLOWBOXSUBST(fc) ((fc)->ExtndFlags & EF_SUBSTHOLLOWBOX_TYPE )
#define	FC_ISLINKEDFONT(fc)		((fc)->ExtndFlags & LINKED_FONT_TYPE )

#define FC_TTERRORCHECK(fc) ((fc)->ExtndFlags & EF_TT_ERRORCHECK_ON)

#define FC_ISTTSCREENRES(fc) ((fc)->ExtndFlags & EF_TT_SCREENRES)

#define FC_ISCID(fc)  ((fc)->ExtndFlags & EF_CID_TYPE1)
#define FC_ISUVS(fc)  ((fc)->ExtndFlags & EF_USE_VARIATION_SELECTOR)
/* "FC_USE_CHARNAME()" tests if "no symbol set mapping" has been requested for either a   */
/* CFF (PostScript or OpenType) or non-CID PostScript font. In these cases, the requested */
/* character is accessed by character name rather than character ID.                      */
#if CFF_RDR
#define FC_USE_CHARNAME(fc) ( !FC_SSMAP((fc)) && \
								( (FC_ISPST1((fc)) && !FC_ISCID((fc)) ) || ((fc))->nameInSet[0] != 0 ) )
#else
#define FC_USE_CHARNAME(fc) ( !FC_SSMAP((fc)) && (FC_ISPST1((fc)) && !FC_ISCID((fc)) ) )
#endif
/**************************************************/
/*** macros to test other fields in FONTCONTEXT ***/
/**************************************************/

#if BOLD_CJK
#define FC_ISBOLD_CJK(fc)	( ((fc)->bold_cjk > 0) )
#endif	/* BOLD CJK */

/* original implementation (using ExtndFlags bit) replaced by	*/
/* new implementation (using special SSNUM value) 				*/
#define FC_ISUSERCMAPTBL(fc) ((fc)->ssnum == USER_CMAP)


/* constants for grid alignment, see 'alignment' in FONTCONTEXT */
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


#define FC_SE_ENGRAVE	0x0001	/* Engrave effect*/
#define FC_SE_EMBOSS	0x0002  /* Emboss effect*/
#define FC_SE_SHADOW	0x0004  /* Shadow effect*/
#define FC_SE_OUTLINE	0x0008  /* Outline effect */
#define FC_SE_OUTLINE_SHADOW 0x0010	/* Combined Outline with Shadow */

#if SPECIAL_EFFECTS
#define FC_SE_ISENGRAVE(fc)	(((fc)->EffectsFlags)& FC_SE_ENGRAVE)
#define FC_SE_ISEMBOSS(fc)	(((fc)->EffectsFlags)& FC_SE_EMBOSS)
#define FC_SE_ISSHADOW(fc)	(((fc)->EffectsFlags)& FC_SE_SHADOW)
#define FC_SE_ISOUTLINE(fc)	(((fc)->EffectsFlags)& FC_SE_OUTLINE)
#define FC_SE_ISOUTLINE_SHADOW(fc)	(((fc)->EffectsFlags)& FC_SE_OUTLINE_SHADOW)
#define FC_SE_ANY(fc)		((fc)->EffectsFlags)
#endif /* SPECIAL_EFFECTS */


#define STIK_FORMAT     (UW16)0x9654
#define STIK_FORMAT_AA     (UW16)0x9655
#define CCC_FORMAT_AA      (UW16)0x0602
#define STIK_CHAR_BIT   0x80

#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)
#define CD_ISSTIKCHAR(cd)	(((*cd) & STIK_CHAR_BIT) == 0)
#define IS_STIK_FONT(p)    ( (p && ((p->stik_font) >= (UW16)1)) ? TRUE : FALSE )
#endif	/* STIK && (TT_DISK || TT_ROM || TT_ROM_ACT) */

#if CCC
#define IS_CCC_FONT(p)    ( (p && (((p->stik_font) == (UW16)2) || ((p->ccc_font) == (UW16)1)) ) ? TRUE : FALSE )
#if FS_EDGE_RENDER
#define IS_CCC_EDGE_FONT(p)    ( (p && ((p->stik_font) == (UW16)3)) ? TRUE : FALSE )
#endif	/* FS_EDGE_RENDER */
#endif
/* Edge ccc outline format */
#define TTF_FORMAT_DDD(a)   ( ( a >= (FS_SHORT)(0x0500) ) &&  ( a <= (FS_SHORT)(0x05FF)))

/* for CCC format */
/* changes for v7 CCC */
/* #define STIK_FORMAT_CCC (UW16)(0x0200) */
/*#define STIK_FORMAT_CCC(a)  ( ( a >= (UW16)(0x0200) ) &&  ( a <= (UW16)(0x02FF) ) ) */
/* changes for v8 CCC */
#define STIK_FORMAT_CCC(a)    ( ( ( a >= (UW16)(0x0200) ) &&  ( a <= (UW16)(0x02FF) ) ) \
								|| \
                                ( ( a >= (UW16)(0x0602) ) &&  ( a <= (UW16)(0x06FF) ) ) )
#define TTF_FORMAT_CCC(a)     ( ( a >= (UW16)(0x0400) ) &&  ( a <= (UW16)(0x04FF) ) )

#define IS_EFM_FONT(fc)    ( (fc)->font_hdr && (!STRCMP( (FILECHAR *)(((fc)->font_hdr) + PCL_CHR_HDR_SIZE), "EXTERNAL FONT MODULE")) )

#ifdef FS_EDGE_TECH
/* user accessible ADF flags */

/* this sets the "adfFlags" field */
#define FLAGS_DEFAULT_CSM_ON  (FS_ULONG)0x00080000
#define FLAGS_DEFAULT_CSM_OFF (FS_ULONG)(~FLAGS_DEFAULT_CSM_ON)

/* this sets the "type" variable, which is accessed in convert_to_graymap */
#define FS_MAP_EDGE_GRAYMAP2  0x0200
#define FS_MAP_EDGE_GRAYMAP4  0x0400
#define FS_MAP_EDGE_GRAYMAP8  0x0800

#define FS_MAP_ANY_EDGE_GRAYMAP (FS_MAP_EDGE_GRAYMAP2|FS_MAP_EDGE_GRAYMAP4|FS_MAP_EDGE_GRAYMAP8)

#endif	/* defined(FS_EDGE_TECH) */


/*-----------------------------------*/
/*       F O N T C O N T E X T       */
/*-----------------------------------*/

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
}  UFST_TYPO;
#endif

#if (SCALE_MATRIX & MAT0_SCALE_MATRIX)
/*  Design space to output space */

typedef struct
{
    SL32    m[4];         /* Design space to output space               */
    SW16    matrix_scale; /* number of fractional bits in m[] values    */
} UFST_MAT0;
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
} UFST_MAT1;
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
} UFST_MAT2;
#endif

/* values for fc_type */

#define FC_TYPO_TYPE      0   /* Typographer                        */
#define FC_MAT0_TYPE      1   /* Matrix design unit to output       */
#define FC_MAT1_TYPE      2   /*        world to output   em box    */
#define FC_MAT2_TYPE      3   /*                          pt, set   */

#define CFFNAMELEN 66	/* add 1 to allow for universal null-termination */

/*-----------------------------------*/
/*       F O N T C O N T E X T       */
/*-----------------------------------*/

typedef struct
{
    SL32    font_id;      /* font number                                */
#if CFF_RDR
    FILECHAR nameInSet[CFFNAMELEN];
#endif /* CFF_RDR */
#if PST1_RDR || (TT_RDR && CFF_RDR)
	/* if this value is set to 0, the field will be ignored; if this value
	is positive, it will replace the hardcoded PS_THRESHOLD value in comp_pix.c
	(which determines when missing-pixel recovery is in effect). */
	UL32	optionalThreshold;
#endif

#if !SCALE_MATRIX    /* typographer version only */

    SW16    point_size;   /* point size of this character in 1/8th pts  */
    SW16    set_size;     /* set   size of this character in 1/8th pts  */
    CGPOINTFX shear;      /* cosine & sine of vertical stroke angle     */
    CGPOINTFX rotate;     /* cosine & sine of baseline rotation         */
    UL32   xres;          /* output pixels per meter                    */
    UL32   yres;

#else

    UW16  fc_type;     /* selector indicates which union type */
    union
    {
#if (SCALE_MATRIX & TYPO_SCALE_MATRIX)
        UFST_TYPO  t;
#endif
#if (SCALE_MATRIX & MAT0_SCALE_MATRIX)
        UFST_MAT0  m0;
#endif
#if (SCALE_MATRIX & MAT1_SCALE_MATRIX)
        UFST_MAT1  m1;
#endif
#if (SCALE_MATRIX & MAT2_SCALE_MATRIX)
        UFST_MAT2  m2;    
#endif
    } s;

#endif	/* SCALE_MATRIX */

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
#if BOLD_CJK
	UW16  bold_cjk;
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

#if TT_RDR
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
#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)
	CGFIXED stroke_pct;
#endif	/* STIK && (TT_DISK || TT_ROM || TT_ROM_ACT) */
#if ARABIC_AUTO_HINT
	BOOLEAN	arabic_auto_hint;
#endif
#if FS_EDGE_RENDER
	FS_FIXED ADFinsideCutoff; 
    FS_FIXED ADFoutsideCutoff; 
    FS_FIXED ADFgamma;
#endif
}  FONTCONTEXT;

typedef FONTCONTEXT * PFONTCONTEXT;


/*----------------------------------*/
/*    Character Bitmap              */
/*----------------------------------*/

#define ISCOMPRESSED_GRAYMAP(pbm)	((pbm)->bppX == 0 && (pbm)->bppY == 0)

/* so that we can access 256-gray-level embedded bitmaps */
#define SET_EMBED_BPP(numSub) ((numSub == 16)? 8 : numSub)


/*----------------------*/
/*  Doubly Linked Lists */
/*----------------------*/
typedef struct
{ 
    MEM_HANDLE  f;  /* forward pointer */
    MEM_HANDLE  b;  /* backward pointer */
} DLL;
typedef DLL * PDLL;

typedef struct
{
    DLL     link;          /* doubly linked list pointers                  */
    UW16   notmru;        /* set to 0 if recently made most recently used */
    HIFFONT hiffont;       /* Font handle of the owner of this IFBITMAP    */
    SL32    index;         /* index to IFBITMAP in above FONT's table      */   /* rjl 9/20/2002 - changed from SW16*/
#if NO_SYMSET_MAPPING && PST1_RDR
	MEM_HANDLE hPSchar_name; /* PostScript character name */
#endif
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
	SL32	pixelWidth;/* "devAdvWidth", shifted to 1/16th pixel    */
			   /* however, this should be INTEGRAL # pixels */

    CGPOINTFX advanceWidth;/* Scaled escapement, 16.16. */
#endif

#if EMBEDDED_BITMAPS && TT_RDR && (CGBITMAP	|| GRAYSCALING)
	BOOLEAN usedEmbeddedBitmap;	/* TRUE if an embedded bitmap was used for char*/
#endif /* EMBEDDED_BITMAPS && TT_RDR && (CGBITMAP || GRAYSCALING) */

#if GRAYSCALING
    SL32    size;          /* Size in bytes of bm[] (actual graymap data, below)*/
#endif

	SW16 topSideBearing;
	UW16 advanceHeight;
	
	/* 
	If we return a bitmap: bppX = bppY = 1
	If we return a regular UFST (compressed) graymap: bppX = bppY = 0
	
	If we return an uncompressed graymap (created by CJK_BOLD, or an embedded graymap): bppX > 1, bppY > 1
	In this case, (bppX * bppY) gives the number of graylevels in the returned uncompressed graymap. 
	*/
			
	UW16 bppX;	/* x bits-per-pixel info for bitmaps and uncompressed graymaps */
	UW16 bppY;	/* y bits-per-pixel info for bitmaps and uncompressed graymaps */
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

typedef IFBITMAP * PIFBITMAP;
typedef IFBITMAP ** PPIFBITMAP;

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
    UL32 num_segmts;
    UL32 num_coords;
    UL32 polarity;
    UL32 segmt_offset;
    UL32 coord_offset;
} OUTLINE_LOOP;
typedef OUTLINE_LOOP * POUTLINE_LOOP;

typedef struct {
  SL32  size;
  SW16  pad;
  UW16 num_loops;
  OUTLINE_LOOP loop[1];  /* actual # of loops depends on character */
  /* Actual outline data follows this array. */
} OUTLINE_CHAR;
typedef OUTLINE_CHAR * POUTLINE_CHAR;

typedef struct
{
    DLL     link;          /* doubly linked list pointers                  */
    UW16   notmru;        /* set to 0 if recently made most recently used */
    HIFFONT hiffont;       /* Font handle of the owner of this IFBITMAP    */
    SL32    index;         /* index to IFBITMAP in above FONT's table      */   /* rjl 9/20/2002 - was SW16 */
#if NO_SYMSET_MAPPING && PST1_RDR
	MEM_HANDLE hPSchar_name; /* PostScript character name */
#endif
#if CACHE && CACHE_BY_REF
	UL32 ref_counter;      /* "cache-by-reference" count */
#endif
#if ASIAN_ENCODING && CACHE
    MEM_HANDLE  f;         /* forward ptr to next IFOUTLINE        */
    MEM_HANDLE  b;         /* backward ptr to next IFOUTLINE       */
#endif
#if XPS_SIDEWAYS
	SL32 TopOriginX;
	SL32 TopOriginY;
#endif	/* XPS_SIDEWAYS */

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
	SL32	pixelWidth;/* "devAdvWidth", shifted to 1/16th pixel    */
                           /* however, this should be INTEGRAL # pixels */
    CGPOINTFX advanceWidth;/* Scaled escapement, 16.16. */
#endif

	SW16 topSideBearing;
	UW16 advanceHeight;

/*** ol MUST be the LAST element in the IFOUTLINE structure - if you need
	to add new elements to the structure, put them before bm[] ***/
/*** this is because of the use of the OLHEADERSIZE definition, below ***/

    OUTLINE_CHAR    ol;    /* changed from 3.01 to match actual data */

} IFOUTLINE;

typedef IFOUTLINE * PIFOUTLINE;
typedef IFOUTLINE ** PPIFOUTLINE;

/* IFOUTLINE header size */
#define OLHEADERSIZE   (sizeof(IFOUTLINE) - sizeof(OUTLINE_CHAR))

	/* The FS_BITMAP and FS_GRAYMAP structs are used by the BOLD_CJK */
	/* pseudo-emboldening functions                                  */
	
typedef struct {
	CACHE_ENTRY *cache_ptr;	/* cache-info structure (private) */
	FS_LONG size;			/* size of structure in bytes, including bits array */
	FS_SHORT lo_x;			/* pixel coordinate of left column, relative to origin */
	FS_SHORT hi_y;			/* pixel coordinate of upper row, relative to origin */
	FS_SHORT i_dx;			/* x-advance in pixels - 0 if rotated or skewed */
	FS_SHORT i_dy;			/* y-advance in pixels - 0 if rotated or skewed */
	FS_FIXED dx;			/* x-advance in fractional pixels (16.16 format) */
	FS_FIXED dy;			/* y-advance in fractional pixels (16.16 format) */
	FS_SHORT width;			/* width of the bitmap in pixels */
	FS_SHORT height;		/* height of the bitmap in pixels */
	FS_SHORT bpl;			/* bytes per line (row) of bitmap data */
	FS_BOOLEAN embedded;	/* was this taken from an embedded bitmap? */
#if FS_EDGE_RENDER
    FS_SHORT bitsPerPixel;  /* bits per pixel of bitmap data (always 1)                 */
    FS_USHORT type;         /* type of glyph data                                       */
#endif	/* FS_EDGE_RENDER */
	FS_BYTE  *bits;			/* bitmap data packed in left-to-right, top-to-bottom order */
	} FS_BITMAP;
	
typedef struct {
    CACHE_ENTRY *cache_ptr; /* cache-info structure (private) */
    FS_LONG size;           /* size of structure in bytes, including bits array */
    FS_SHORT lo_x;          /* pixel coordinate of left column, relative to origin */
    FS_SHORT hi_y;          /* pixel coordinate of upper row, relative to origin */
	FS_SHORT i_dx;			/* x-advance in pixels - 0 if rotated or skewed */
	FS_SHORT i_dy;			/* y-advance in pixels - 0 if rotated or skewed */
    FS_FIXED dx;            /* x-advance in fractional pixels (16.16 format) */
    FS_FIXED dy;            /* y-advance in fractional pixels (16.16 format) */
	FS_SHORT width;			/* width of the graymap in pixels */
	FS_SHORT height;		/* height of the graymap in pixels */
    FS_SHORT bpl;           /* bytes per line (row) of graymap data */
	FS_BOOLEAN embedded;	/* was this taken from an embedded graymap? */
#if FS_EDGE_RENDER
    FS_SHORT bitsPerPixel;  /* bits per pixel of graymap data (2,4,8)                    */
    FS_USHORT type;         /* type of glyph data                                        */
#endif	/* FS_EDGE_RENDER */
    FS_BYTE *bits;          /* graymap data packed in left-to-right, top-to-bottom order */
	} FS_GRAYMAP;

#ifdef FS_EDGE_TECH
typedef struct {
    CACHE_ENTRY *cache_ptr; /* cache-info structure (private)                          */
    FS_LONG size;           /* size of structure in bytes, including bits array        */
    FS_SHORT lo_x;          /* pixel coordinate of left column, relative to origin     */
    FS_SHORT hi_y;          /* pixel coordinate of upper row, relative to origin       */
    FS_SHORT i_dx;          /* x-advance in pixels - 0 if rotated or skewed            */
    FS_SHORT i_dy;          /* y-advance in pixels - 0 if rotated or skewed            */
    FS_FIXED dx;            /* x-advance in fractional pixels (16.16 format)           */
    FS_FIXED dy;            /* y-advance in fractional pixels (16.16 format)           */
    FS_SHORT width;         /* width of the graymap in pixels                          */
    FS_SHORT height;        /* height of the graymap in pixels                         */
    FS_SHORT bpl;           /* bytes per line (row) of glyphmap data                   */
    FS_BOOLEAN embedded;    /* was this an embedded glyph?                             */
    FS_SHORT bitsPerPixel;  /* bits per pixel of glyphmap data                         */
    FS_USHORT type;         /* type of glyph data                                      */
    FS_BYTE bits[1];        /* glpyh data                                              */
    } FS_GLYPHMAP;
#endif	/* defined(FS_EDGE_TECH) */
		
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
typedef IFTILE * PIFTILE;


/*----------------------------------*/
/*    Font Metrics Structure        */
/*----------------------------------*/
#define FNT_MET_NAM_LEN  50

#define IFFAMILYNAME_LENGTH   20    /* size taken from FAIS data specification */
#define IFTYPEFACENAME_LENGTH 50    /* size taken from FAIS data specification */

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

typedef struct
   {
   union
      {
      UL32  TT_unicode;
      UL32  MT_unicode;
      UL32  IF_cgnum;
      VOID *PS_name;
      } CharType;
   }  WIDTH_LIST_INPUT_ENTRY;
typedef WIDTH_LIST_INPUT_ENTRY *PWIDTH_LIST_INPUT_ENTRY;

typedef struct
   {
   UW16  CharWidth;
   UW16  ScaleFactor;
   }  WIDTH_LIST_OUTPUT_ENTRY;
typedef WIDTH_LIST_OUTPUT_ENTRY *PWIDTH_LIST_OUTPUT_ENTRY;

typedef struct
   {
   MEM_HANDLE DLBitmapPtr;
   } DLBITMAPPTR;

typedef DLBITMAPPTR * PDLBITMAPPTR;


/* Structure used to return UFST version information to the user -
	current values for the UFST-version fields are defined in cgif.c,
	just above the API call CGIFwhat_version()
*/

#define PATCHSTRING_LEN 30
#define UFST_VERSION_MAJOR 7
#define UFST_VERSION_MINOR 2

#define ufst_version_patchstring ""

typedef struct
{
	UW16 major_version;
	UW16 minor_version;
	FILECHAR patchstring[PATCHSTRING_LEN];	
} 
UFST_VERSION_INFO;

#endif	/* __CGIF__	*/
