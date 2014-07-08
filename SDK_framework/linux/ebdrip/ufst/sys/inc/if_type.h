/* $HopeName: GGEufst5!sys:inc:if_type.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2004 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/sys:inc:if_type.h,v 1.4.4.1.1.1 2013/12/19 11:24:03 rogerb Exp $ */
/* if_type.h */

/* History
 *    05-May-90 awr  moved char_buf into IF_CHAR structure
 *    15-Jun-90 awr  added max_des field to COORD_DATA
 *    28-Jun-90 bjg  added OUTLINE_PARAMS structure definition. 
 *    27-Jul-90 awr  corrected structure sizes in comments
 *    29_Nov-90 tbh  Changed COORD_DATA structure by adding field 
 *                   "st_variation[4]"
 *    09-Dec-90 awr  added fields in COORD_DATA for new tx() ty() calcs.
 *    15-Jan-91 jfd  Changed field "baseline_offset" in structure IF_CHAR
 *                   from UWORD to WORD.
 *    20-Dec-90 dET  add support for multi-model MSC compilation.
 *                   changed COORD_DATA comment to reflect 66 bytes.
 *    10-Feb-91 awr  Made global flag char_is_if part of IF_CHAR structure.
 *                   Made Global or_on part of IF_DATA structure.
 *                   Added making_bold to IF_DATA structure.
 *     9-Mar-91 awr  Removed OUTLINE_PARAMS.
 *    25-Apr-91 awr  reorg'd if_type to distinguish IF Scaling Intelligence
 *                   stuff from output scaling.
 *    19-May-91 awr  Changed IF_CHAR structure for next_loop()
 *     3-Jun-91 awr  HQ4 changes
 *     9-Jun-91 awr  removed bmwidth & depth from SCALE structure
 *    16-Jun-91 awr  changed XY structure references to WORDVECTOR
 *     4-Jul-91 awr  Removed COORD_DATA "max_des" field. No longer used.
 *                   In SCALE added "wrong_read" field
 *                            changed "quadrant" from UWORD to WORD
 *                            removed "px" and "py"
 *     5-Aug-91 bjg  Changed "defined(ELASTIC... to (ELASTIC....
 *    25-Aug-91 awr  Moved function decls from c modules
 *     3-Sep-92 awr  removed scale.tt0 and reduced scale.m[] to 4 elements
 *    24-Nov-91 awr  added log_xpix & log_ypix defines
 *     7-Mar-92 awr  added CHAR_STATS to if_stat
 *    19-Mar-92 awr  Moved BOX to port.h.
 *    02-Apr-92 jfd  Added diag_control_tol and margin to COORD_DATA
 *                   Added stan_STAN_i to DIMENSION
 *                   Added stan_STAN_dim_lim to IF_STATE
 *    02-Apr-92 rs   Portability cleanup (see port.h).
 *    12 May 92 ss   Changed conditional compile of EXTERN_FONT to use the
 *                   new define NON_IF_FONT.
 *    14-Jun-92 awr  Changed type of xlate, and tbound to INTRs
 *    18-Jul-92 awr  Added est_ouline flag to if_state structure.
 *    21-Jul-92 awr  Conditional compile changes.
 *    07-Aug-92 rs   Compatibility w/Windows 3.1 (FIXED, POINTFX).
 *    14-May-93 jd   In structure IF_STATE, conditionally compiled
 *                   "pathname" and "font_name" based on
 *                   (PST1_DISK || TT_DISK || (DYNAMIC_FONTS && IF_DISK))
 *                   to resolve compiler errors.
 *    01-Jul-93 maib changed making_bold to non_z_wind
 *    09-Jul-93 awr  Added back making_bold to IF_STATE structure
 *    22-Sep-93 mby  Add IF_STATE.usePlugins; ROM1 support.
 *                   Remove IF_STATE.extern_font (never used).
 *    03-Feb-94 mby  For CONVERGENT_FONTS, added awtsub_on (used to be in
 *                   FONTINDEX structure) and lsb_adj (horizontal offset)
 *                   to IF_STATE.
 *    06-Mar-94 mby  Convergent fonts: in IF_STATE, lsb_adj is conditionally compiled.
 *    26-Apr-94 mby  Added IF_STATE.shear to detect situations of orthogonal
 *                   rotation and nonzero shear.
 *    26-May-94 mby  FCO change from 1.6.1.1:
 *                     Added if_state.FCObject - FCO font handle.
 *    27-Jan-95 mby  Added 'fcoCleanup' flag to IF_STATE structure.
 *    18-Apr-95 jfd  Added 'chIdptr' and 'PSchar_name' fields to IF_STATE 
 *                   structure (loaded by CGIFchIdptr() call).
 *    26-Aug-96 mby  Added IF_STATE.FCO_PluginH (plugin FCO handle, moved here from ix.h).
 *    18-Sep-96 mby  Added scaling constants for escapement computations.
 *    02-Oct-96 mby  Adjust scaling constants slightly to match HP's results.
 *    10-Jan-97 dlk  Removed ELASTIC_X and ELASTIC_Y options as part of
 *                   project to trim ufst.  Removed function prototypes for
 *                   elastic pixel_align() and sd_pixel_align.
 *    13-Jan-97 dlk  Removed CONVERGENT_FONTS option as part of project to trim
 *                   ufst.
 *    17-Mar-97 keb  Added two fields to if_state to capture plugin search levels
 *    22-Apr-97 mby  In COORD_DATA & IF_STATE structures, conditionally
 *                   removed various elements not needed except by IF_RDR
 *                   or FCO_RDR.
 *    29-May-97 sbm  Add TT_ROM_ACT functionality.
 *    29-Jul-97 awr  Added fields "startx" and "starty" to SMEAR_INSTANCE
 *                   structure.
 *    04-Sep-97 keb  Removed all references to BJG_BOLD_P6
 *    23-Jan-98 awr  Added typedef PIF_STATE. Moved data into if_state
 *                   for re-entrant. Very large character changes.
 *    03-Feb-98 dlk  Added defines for constants to convert PostScript metric
 *                   data to TrueType units (1000->2048) for HP4000 Emulation
 *                   (PSDESIGNUNITS, PS2TTMUL, PS2TTDIV, and PS2TTADD).
 *    23-Feb-98 awr  Removed (unused) if_state.glob_ital_tan
 *                   Added if_state.DECEND and adjust_angle for ASIANVERT
 *	  30-Mar-98 slg	 Comment out unused IF_STATE elts = contour_data,
 *					 subst_wdth_fac, alt_black_width, alt_black_height,
 *					 contour_flag, PreBrokenContrs.
 *					 Make lots of IF_STATE elts IF_RDR-only.
 *					 Move ADJUSTED_SKEL typedef here from adj_skel.h.
 *	  			     Move MLOCALs from extmem.c, intmem.c, cache.c, bucket.c,
 *					 lineto.c, cmpr.c, if.c, kanmap.c, skeletal.c, pcl_swap.c,
 *					 symmap.c into IF_STATE.
 *	  31-Mar-98 slg	 Move MLOCALs from tt_if.c,	mixmodel.c,	fc_if.c,
 *					 graymap.c, imagegr.c into IF_STATE.
 *	  01-Apr-98 slg	 Move MLOCALs from cubic.c, fc_intfl.c into IF_STATE. Also
 *					 move GLOBALs from cache.c, intmem.c, extmem.c (+ CGIFinitstate).
 *	  02-Apr-98 slg	 Move GLOBALs from bitmap.c, maker.c, graymap.c, cgif.c,
 *					 fpmath.c, outdata.c, bucket.c, path.c, fm.c, manipula.c
 *					 into IF_STATE.	Move TRAN typedef from tr_type.h.
 *					 Move CHARWIDTH typedef from segments.h.
 *	  14-Apr-98 slg	 Move GLOBAL fontindex from ix.c; pathname must be !ROM;
 *					 move GLOBAL special_case from quadra.c.
 *	  15-Apr-98 slg	 Move GLOBALs: chr_def[], chr_def_hdr from chr_def.c;
 *					 symbolset, symbol_setIF[], symbol_setTT[], symbol_setMT[]
 *					 from symmap.c; dup_points[] from if_init.c;
 *					 hchar_buf from fm.c; 
 *    19-May-98 keb  Added ENTITYACT define for TT_ROM_ACT functionality
 *    24-May-98 awr   Added comments and new field "numleft"
 *                    to BITSERVE_IM to fix bogus memory accesses in
 *                    gichar()
 *	  29-May-98 slg  Move /psi vbls: MLOCALs and GLOBALs from t1isfnt.c, 
 *					 t0ikan.c, t1idecod.c, t1ihints.c, t1iscan.c.
 *    12-Jun-98 slg  Delete "fcoCleanup" field of IF_STATE (never set), remove
 *					 MULTICALLER data; move F_ONE def here; add peculiar 
 *					 "FSPvoid" parameter to OUT_TBL typedef, to solve circular-
 *					 reference problem (because OUT_TBL, which is used within
 *					 IF_STATE typedef, must point to an instance of IF_STATE).
 *	  22-Jun-98 slg	 "pix_al_ptr" function-ptr type change for reentrancy; move
 *					 several fn prototypes into shareinc.h
 *	  08-Jul-98 slg  Move XLfont, lots of Surfer vbls into IF_STATE.
 *    09-Jul-98 dlk  Integrated new CFF processing support into UFST code base.
 *                   Changed copyright notice dates.
 *	  09-Jul-98 slg  Remove first_loop/next_loop prototypes again (these were
 *					 moved into shareinc.h)
 *    05-Aug-98 awr  Changed all !ROM to DISK_FONTS
 *	  31-Aug-98 slg	 Move GRAYIMAGE definition from imagegr.h
 *    01-Oct-98 tbh  Removed rstack
 *    11-Dec-98 keb  Added code to support ASIANVERT for XLfonts
 *	  06-Jan-99 slg	 Arrange IF_STATE elements somewhat less arbitrarily: 
 *					 put all elements that are not conditionally-compiled at 
 *					 start of structure; group all related conditionally-compiled
 *					 elements together; improve packing of structs somewhat
 *					 (the real size-reduction payoff will occur if we can turn
 *					 "int" elements into "SW16" elements, however). Also delete 
 *					 "width_mixm" (used only in obsolete "mixmodel.c").
 *	  19-Jan-99 slg	 Include contents of former cache.h, bitmap.h - moved
 *						into if_type.h to resolve customer name conflict
 *    26-Jan-99 keb  Removed ASIANVERT condition from definition of xl_char_class
 *      July-99 swp  Replaced the NZW apparatus,  enabled if SWP799 is defined.
 *	  17-Jan-00 slg	 Vertical-writing changes (for keb): remove DECENDER,
 *					 adjust_angle, XLfont, xl_char_class from IF_STATE; 
 *					 add charVertWrit, have_adjusted_angle to IF_STATE.
 *	  31-Jan-00 slg  Integrate disk/rom changes (for jd) - add "font_access"
 *					 to IF_STATE; add DISK_ACCESS / ROM_ACCESS defines.
 *    03-Feb-00 awr  Changed SWP799 to WINDCOMP
 *    10-Feb-00 awr  Removed PST1_ROM
 *    24-Mar-00 jwd  Cache w/NO_SYMSET_MAPPING fix.
 *    28-Sep-00 jfd  In IF_STATE, added fields 'BitmapManipulated' and 'BitmapEmbedded'.
 *	  05-Dec-00 slg  Add new typedefs for CGIFtt_cmap_query() function: internal 
 *					 structs CMAP_SUBTABLE & CMAP_HEAD; return-info structs 
 *					 CMAP_ENTRY & CMAP_QUERY; MAX_NUM_CMAP define.
 *    02-Apr-01 awr  Return error if PS path doesn't start with a moveto
 *    02-Apr-01 awr  Removed fixed size array from ACT memory management
 *    19-Apr-01 awr  Added fields to NZ_NODE & NZ_instance to find wrong wound contours
 *	  04-May-01 slg  Non-UFST data-type cleanup; get rid of AGFATOOLS hack;
 *					 tighten up conditional compile for embedded bitmaps.
 *    24-May-01 swp  removed T1I_HINT_STR, NUMHINTS from 50 to 96, and hinttype
 *					 enabled by #define NEW_PS_HINTS
 *    24-Aug-01 jfd  Added PCL_font_type field to IF_STATE.
 *    27-Aug-01 jfd  Moved "PCL_font_type" field outside of BYTEORDER block.
 *    06-Dec-01 jfd  Added new fields "stik_font" and "stik_char" to IF_STATE structure.
 *    16-Jan-02 jfd  Removed conditional compiles surrounding "stik_font" and "stik_char"
 *                   in IF_STATE.
 *    07-Feb-02 jfd  Moved "stik_font" and "stik_Char" fields from IF_STATE to fs_GlyphInfoType
 *                         structure.
 *    27-Aug-02 jfd  Added "lpm" field to IF_STATE structure.
 *    23-Sep-02 jwd  Added OPTIMIZE_FCOACCESS constants and macros.
 *    25-Sep-02 jfd   Added stroke font refline fields to IF_STATE.
 *    23-Oct-02 jfd   Added fields "xlpm" and "ylpm" to IF_STATE (moved from FONTCONTEXT).
 *    04-Nov-02 jfd   Added missing character substitution support.
 *    07-Jan-03 jfd  Added "update_reflines_flag_set" field to IF_STATE.
 *    12-Jun-03 jfd  Added "cache-by-reference" support.
 *    13-Aug-03 jfd  Added "table-by-reference" support.
 */

#ifndef __IF_TYPE__
#define __IF_TYPE__

/* The OUTL_COORD type is used for "outl_XYdata", below */
#if VLCOUTPUT
typedef SL32 OUTL_COORD;
#else
typedef SW16 OUTL_COORD;
#endif

/* Asian-cache constant */

#define USE_LOW_BYTE      1  /* set to 1 to use lo-byte of chId as offset */
                             /* into hash table for better distribution   */

/*** START moved from cache.h ***/

#if CACHE

#define HASHMAX         256   /* max rows to contain column hash lists */
#define SSMAX         256


/*----------------------*/
/*      Font            */
/*----------------------*/

typedef struct
{
    DLL   link;           /* doubly linked list pointers- this slot may be
                           * in an active least recently used list or it may
                           * be unused in the avail list.
                           */
    UW16 bitmapCount;    /* Number of bitmaps in this font */
    FONTCONTEXT fc;
#if DISK_FONTS
    PATHNAME pathname;
#endif
#if COMPRESSED_CACHE
    HIFBITMAP hifbm;
    PIFBITMAP pifbm;
    SL32 size;     /* size of pifbm->bm[] */
#endif

#if UFST_MULTITHREAD
	UL32 thread_ids[MAX_THREADS];
#endif
/*
 * 1/25/94 maib
 * new caching scheme for asian encoding
 */
#if USE_ASIAN_CACHE
    MEM_HANDLE hash_table[HASHMAX]; /* hash table */
#else
    HIFBITMAP hbuf[SSMAX];  /* buffer handles of cached characters */
#endif
}
cacheFONT;
typedef cacheFONT FARPTR * PFONT;

#endif /* CACHE */

/*** END moved from cache.h ***/

/*** START moved from bitmap.h ***/


#if NON_Z_WIND
#ifdef WINDCOMP	
/* new non-zero winding structures */
typedef struct {
	SL32 x;			/* the x coordinate of the transition */
	SL32 next;			/* the index of the next NZNODE in the raster */
	} NZNODE;
typedef struct {
	SW16 num;			/* number of slots in <indices> */
	SL32 next_index;	/* next available node number */
	SL32 stop_index;	/* 1+maximum node number */
    MEM_HANDLE htrans;
    MEM_HANDLE hindices;
    MEM_HANDLE hnodes;
    SL32 *indices;		/* the starting node for each row of bitmap */
	NZNODE *nodes;
	} NZTRANS;
#else /* ! WINDCOMP */

typedef struct NZ_NODE
        {
            struct NZ_NODE FARPTR * link;
            PNZCOUNTER  pval;
            NZCOUNTER   onoff;
            NZCOUNTER   lowRow;
            NZCOUNTER   valCount;
#if FIX_CONTOURS
            /* for detecting badly wound contours */
            SL32 right;        /* Number of transitions that matched our expectations */
            SL32 wrong;        /* Number we thought were on but were really off or vice versa */
            SW16 contourNum;   /* Contour that this transition run belongs to */
#endif /* FIX_CONTOURS */
        } NZ_NODE;

typedef NZ_NODE FARPTR * PNZ_NODE;

typedef struct
{
    NZCOUNTER  tran_run_ct;  /* number of transition runs = number of
                              * changes in y direction in character outline
                              */
    SW16       blackdepth;   /* longest transition run */
    NZ_NODE    pend_list;    /* head nodes for linked list */
    NZ_NODE    active_list;
    NZCOUNTER  start_row; /* startins row of current run */
    NZCOUNTER  valCount; /* number of transitions in current run */
    NZCOUNTER  overflow; /* we tried to write too many runs */
    NZCOUNTER  done;     /* all nodes exhausted; arms for overflow err */
    PNZ_NODE   nodes;  /* array of tran_run_ct nodes */
    NZCOUNTER  cur_node; /* index of current node */
	PNZCOUNTER transition; /* One row of transitions- starts just after nodes[] */
    PNZCOUNTER pvalues;   /* array of pvalues- starts just after transitions[] */
    PNZCOUNTER pval;      /* next place to write a transition value */
#if FIX_CONTOURS
    SW16 contourNum;        /* current contour number - copy into all new NZ_NODES */
    /* Next is for detecting badly wound contours */
    MEM_HANDLE hisWrong;    /* array indexed by contourNum, Set to 1 if the contour */
    LPUB8 isWrong;          /* is wound wrong; that is if black isn't on the right. */
    SW16 checkingContours;  /* Set if FC_ISCONTCHK(&if_state.fcCur)) and we're doing pcl6 bold,
                             * bold horizontal or missing pixel (or_on). Means we will check
                             * the characters contours for correct direction and build the
                             * array isWrong[]
                             */
#endif
} NZ_INSTANCE;
typedef NZ_INSTANCE FARPTR * PNZ_INSTANCE;
#endif /* WINDCOMP */
#endif /* NON_Z_WIND */


typedef struct
{
    CHAR_STATS o;
    LPUB8      tran_buffer;
    LPUB8      or_buffer;
    SW16VECTOR bmdim;     /* bitmap dimensions */
#if NON_Z_WIND

#ifdef WINDCOMP
	NZTRANS *trans;			/* new non-zero-winding aparatus */
#else /* !WINDCOMP */
    NZCOUNTER  tran_run_ct;  /* number of transition runs = number of
                              * changes in y direction in character outline
                              */
    NZ_INSTANCE nz_instance;
#endif /* WINDCOMP */
#endif

    UW16       polarity;
    SL32         ydir;
    SL32        tsize;       /* transition array size in bytes */
    SL32         dytran;
    SL32         yraster;
    SL32        toff;        /* offset to current raster in trans array */
    SL32         xx;          /* Bit offset into raster of transition */

#ifndef WINDCOMP
	/* only needed for old NZW aparatus */
   /* Missing Pixel Recovery */

    BOOLEAN     beam;         /*  TRUE if current segment makes on trans */

    BOOLEAN     first_tran;   /*  TRUE until 1st transition is produced  */

    SL32         ptran;        /*  previous transition: last xx produced  */
                              /*  by previous line segment.              */
    BOOLEAN     seton;        /*  Current line segment is producing on   */
                              /*  transitions.                           */
    BOOLEAN     wason;        /*  Previous line segment was producing on */
                              /*  transitions.                           */

        /*  Values when first transition is produced. These values are
         *  used to close the loop by linking the last transition in
         *  the loop to the first transition.
         */

    SL32         ydir1;
    BOOLEAN     seton1;
    SL32         xx1;
    SL32         dytran1;
    SL32        toff1;
#endif /* !WINDCOMP */

#if TILE
    SL32  above;
    SL32  below;
    INTR clip_fops_top;
    INTR clip_fops_bot;
#endif
} RASTER;

/*****************************************/
/*                                       */
/*      Raster Line Organizations        */
/*                                       */
/*****************************************/
#if (RASTER_ORG == EIGHT_BIT_CHUNK)

typedef UB8           CHUNK;
typedef UB8  FARPTR * LPCHUNK;
#define CHUNK_COUNT   1
#define CHUNK_SHIFT   3
#define CHUNK_MASK    7

#elif (RASTER_ORG == SIXTEEN_BIT_CHUNK)

typedef UW16          CHUNK;
typedef UW16 FARPTR * LPCHUNK;
#define CHUNK_COUNT   2
#define CHUNK_SHIFT   4
#define CHUNK_MASK    15

#elif (RASTER_ORG == THIRTYTWO_BIT_CHUNK)

typedef UL32          CHUNK;
typedef UL32 FARPTR * LPCHUNK;
#define CHUNK_COUNT   4
#define CHUNK_SHIFT   5
#define CHUNK_MASK    31

#endif  /* RASTER_ORG == EIGHT_BIT_CHUNK */



/*---------------------------------------*/
/* Constants for escapement computations */
/*---------------------------------------*/
#define  IFDESIGNUNITS  8782
#define  PSDESIGNUNITS  1000

#define  IF2TTMUL       3335L    /* Constants to scale DOWN by 0.23221 */
#define  IF2TTDIV       14362L   /* (8782 design units to 2048).  */
#define  IF2TTADD       7181L    /* .2322099986 */

#define  ADJWIDTHMUL    12500L   /* Constants to convert FCO metrics */
#define  ADJWIDTHDIV    12447L   /* from non-8782 to 8782 units if   */
#define  ADJWIDTHADD    6223L    /* DU_ESCAPEMENT = 0 */

#define  PS2TTMUL       4194L    /* Constants to scale UP by 2.048 */
#define  PS2TTDIV       2048L    /* (1000 design units to 2048).  */
#define  PS2TTADD       1024L    /* 2.048 */
                                 /* (72*2048) / (72*1000) = 2.048 */
                                 /* 2048*2.048=4194.304= 4194 */
                                 /* 1000*2.048=2048 */
                                 /* 2048*.5=1024 */

/* vertex code macros */

#define V_CONTACT(v)  (BOOLEAN)(((v)&1) == 0)
#define V_XSKEL(v)    ((v)&4)
#define V_YSKEL(v)    ((v)&8)



/*---------------------------------------------------------------------*/
#ifdef LINT_ARGS

typedef struct {
    UW16 (*start_char) (FSPvoid PVOID, PVOID);
    UW16 (*end_char)   (FSPvoid PVOID);
    UW16 (*start_loop) (FSPvoid PVOID, UW16);
    UW16 (*end_loop)   (FSPvoid PVOID);
    UW16 (*moveto)     (FSPvoid PVOID, INTR, INTR);
    UW16 (*lineto)     (FSPvoid PVOID, INTR, INTR);
    UW16 (*quadto)     (FSPvoid PVOID, INTR, INTR, INTR, INTR);
    UW16 (*cubeto)     (FSPvoid PVOID, INTR, INTR, INTR, INTR, INTR, INTR);
} OUT_TBL;
typedef OUT_TBL FARPTR * POUT_TBL;

#else  /* !LINT_ARGS */

typedef struct {
    UW16 (*start_char) ();
    UW16 (*end_char)   ();
    UW16 (*start_loop) ();
    UW16 (*end_loop)   ();
    UW16 (*moveto)     ();
    UW16 (*lineto)     ();
    UW16 (*quadto)     ();
    UW16 (*cubeto)     ();
} OUT_TBL;
typedef OUT_TBL FARPTR * POUT_TBL;

#endif /* LINT_ARGS */

typedef struct
{
    PSW16VECTOR cv;         /* coordinates (contacts are interleaved)     */ 
    LPSB8       v;          /* vertex codes                               */
    UW16       npnt;       /* number of curve points                     */
    UW16       ncontact;   /* number of contact points                   */
    UW16       ncoords;    /* number of vector coordinates               */
    SB8        polarity;
}
LOOP;
typedef LOOP FARPTR * PLOOP;


/*---------------------------------------------------------------------*/
/*                      S m e a r                                      */
#if BOLD_HORIZONTAL || BOLD_VERTICAL || BOLD_P6
typedef struct SMEAR_INSTANCE
{
    FILECHAR *name;
    SL32VECTOR xaxis;
    SL32VECTOR yaxis;
    SL32 dir;            /* +1 : */
                         /* -1 : */
    SL32 firstdir;    /* direction of first lineto() */

    /* Hook into render chain */
    PVOID p;       /* instance */
    OUT_TBL out;   /* function table containing starchar(), lineto() ...*/

    /* First curve element */
    SL32 isFirst;
    INTR p0x, p0y;   /* My callers last lineto() or moveto() */
    INTR startx, starty;   /* My caller's moveto() */
    UW16 polarity;
    BOOLEAN beam;    /* TRUE if current lineto is not in shift mode */
                     /* FALSE if shift mode is on                   */
    BOOLEAN firstbeam;
} SMEAR_INSTANCE;

typedef SMEAR_INSTANCE FARPTR  * PSMEAR_INSTANCE;
typedef PSMEAR_INSTANCE FARPTR * PPSMEAR_INSTANCE;
#endif /* BOLD_HORIZONTAL || BOLD_VERTICAL || BOLD_P6 */



/*                        C O O R D _ D A T A                          */
/*---------------------------------------------------------------------*/
/*                             S C A L E                               */

/*  NOTE: that all macros work properly regardless
 *        of the sign of the argument.
 */
#define log_xpix      if_state.x.grid_shift
#define log_ypix      if_state.y.grid_shift
#define xpix          if_state.x.pixel_size
#define ypix          if_state.y.pixel_size
#define half_xpix     if_state.x.half_pixel
#define half_ypix     if_state.y.half_pixel
#define XCEILING(v)   (1 + (((v) - 1) >> if_state.x.grid_shift))
#define XFLOOR(v)     ((v) >> if_state.x.grid_shift)
#define YCEILING(v)   (1 + (((v) - 1) >> if_state.y.grid_shift))
#define YFLOOR(v)     ((v) >> if_state.y.grid_shift)
#define XPIXEL(v)     (((v) + half_xpix) >> if_state.x.grid_shift)
#define YPIXEL(v)     (((v) + half_ypix) >> if_state.y.grid_shift)


typedef struct
{
  /* -----------Intellifont Scaling Intelligence  values----------- */

    INTR    grid_align;     /* x & grid_align is truncated to grid  */
    SW16    orig_pixel;     /*  Original imprecise pixel size       */
#if IF_RDR || FCO_RDR
    SW16    p_pixel;        /*  precise pixel - shifted so that     */
                            /*    16K <= p_size < 32K               */
    SW16    p_half_pix;     /*  half precise pixel                  */
    SW16    bin_places;     /*  bits after the binary point above   */
#endif  /* IF_RDR || FCO_RDR */
#if IF_RDR
    SL32    round[4];       /*  rounding value for 4 "R" types      */
    SW16    con_size;       /*  min value for constrained dim       */
    SW16    diag_control_tol;   /*  DWU tolerance for diag control  */
#endif  /* IF_RDR */


  /* --------------- Standard Dimensions -------------------------- */
#if IF_RDR
    SW16    stand_value;    /* Standard dimension in design units   */
    SW16    st_value[4];    /* These two arrays are results of      */
    SW16    st_pixels[4];   /* pixel_align() using all 4 R-types    */
    SW16    st_variation[4];/* Amount stan dim rounds 11-29-90 tbh  */
#endif  /* IF_RDR */

  /* --------------  Scaling to Output Space ---------------------- */

    SW16    pixel_size;     /* pixel dimension (power of 2)         */
    SW16    half_pixel;     /* half pixel dimension (power of 2)    */
    SW16    grid_shift;     /* x >> grid_shift is grid number       */
#if IF_RDR
    SW16    shift;          /* 16 - bin_places - grid_shift         */
    SW16    shift_rnd;      /* 2 ** (shift - 1)                     */
#endif  /* IF_RDR */

}  COORD_DATA;
typedef COORD_DATA FARPTR * PCOORD_DATA;

/* Save a comp_pix context to see what changes to next time. */
typedef struct {
   UW16       masterPt;            /* master point size    */
   UW16       xRes;
   UW16       yRes;
#if FCO_RDR
   UW16       orThreshold;         /* 10-13-94, mby */
   SB8        compositionFlag;     /* 0=IF compatible; 1=TT compatible */ 
#endif  /* FCO_RDR */
#if SLIM_FONTS
   UB8        mirflags;            /* mirror_flags   */
#endif
} COMP_PIX_CONTEXT;

/*  Values for quadrant field below.
 * A value of 0 means arbitrary rotation.
 */
#define ROT0    1
#define ROT90   2
#define ROT180  3
#define ROT270  4


/*** start of defines/ typedefs for UFST reentrancy ***/

#define DISK_ACCESS	0
#define ROM_ACCESS	1

/* moved here from fpmath.h */
#define F_ONE   0x10000L

/* moved here from /sys/inc/segments.h */
/*--------------------------------*/
/*  Character Width Segment (104) */
/*--------------------------------*/
typedef struct
{
    UW16 width;
    UW16 flags;

} CHARWIDTH;
typedef CHARWIDTH FARPTR * PCHARWIDTH;

/* moved here from intmem.c, extmem.c */
#define NP    3                /* number of memory pools      */

#if INT_MEM_MGT  /*  internal memory manager  */
/* moved here from intmem.c */
#define NH           	9      /* number of handles */

typedef struct
{
    SL32        size;    /* size of free block */
    MEM_HANDLE  next;    /* next free block */
} MEMHEAD;
typedef MEMHEAD FARPTR * PMEMHEAD;

typedef struct
{
    MEMHEAD  nullblock[NP];    /* empty mem blocks for initialization      */
} PAGE0;

typedef struct
{
    SL32    size;        /*  size of block given to us thru CGIFfund() */
#if (HUGE_PTR_SUPPORT)
    HPSB8   ptr;         /* current address of this block */
#else
    LPSB8   ptr;         /* current address of this block */
#endif
    UW16   pool;        /* memory pool that block belongs to */
} MEM;
typedef MEM FARPTR * PMEM;
#endif	/* INT_MEM_MGT */

#if (CGBITMAP)
/* moved here from /bmp/lineto.c */
typedef struct tran_struct {
	SL32			toff;
	SL32			ydir;
	SL32			xx;
	SL32			dytran;
	BOOLEAN		seton;
} Tran_lineto;

typedef struct {
	SL32		toff;
	SL32			xx;
	SL32			ydir;
	SL32			ptran;
	SL32			yraster;
	SL32			dytran;
	BOOLEAN		beam;
	BOOLEAN		seton;
	BOOLEAN		wason;
} State_lineto;
#endif	/* CGBITMAP */

#if COMPRESSED_CACHE
/* moved here from /dep/cmpr.c */
/*  Bitserver.  This object represents a stream of bits. */
typedef struct
{
    SW16  error;
    UW16 *wordstream;   /* array of maxwordsinstream UW16s        */
    UL32  bitbuf;       /* bit buffer, read or write next UW16    */
                        /* from wordstream[numused]               */
    SW16  bitct;         /* number of bits stored in bitbuf       */
    INTG  numused;       /* num UW16's read or written            */
    INTG  maxwordsinstream;  /* number of UW16's in the stream    */

    SW16  bitWidth; /* word width in bits for putbits() & getbits() */
    UL32  mask;     /* bitbuf & mask is the next bitWidth bits in stream */
} BITSERV_CMPR;
#endif	/* COMPRESSED_CACHE */

#if OUTLINE
/* moved here from /out/outdata.c */
typedef struct
{
    INTR p0x, p0y;   /* My callers last lineto() or moveto() */
    CHAR_STATS s;             /* copy of outline stats */
#if OLDATA
    HIFOUTLINE hol;   /* Handle of outline being constructed */
    PIFOUTLINE pol;   /* pointer "   "       "        "      */

    POUTLINE_CHAR outcharptr;
    POUTLINE_LOOP oloop;      /* current loop under construction */
    LPSB8 segptr;             /* next segment in outline data structure */
    PINTRVECTOR pntptr;       /*  "   point   "     "      "      "     */

    UW16 format;      /* */
    SW16 tot_pnts;    /* total number of points generated   */
    SW16 tot_segs;    /* total number of segments generated */
#else
    VOID *pClientHook;  /* Adobe */
    VOID *pPathProcs;
    VOID *pCharIO;
    INTG  shiftToFixed;  /* shift fop data left to get 16.16 */
#endif
} ODATA;
typedef ODATA FARPTR * PODATA;
#endif	/* OUTLINE */



#if GRAYSCALING
/* moved here from /gray/graymap.c */
#define MAXGSCOUNT    5
typedef struct
{
    UL32 maxbytestream;
    MEM_HANDLE bytestream;
    LPUB8 p_bytestream;
    UINTG numused;
    UW16 bitbuf;
    UB8  bitct;
} BITSERVE_GM;

/* moved here from /gray/imagegr.c */
/* Bit server: converts a stream of bytes to a stream of bits.	*/
typedef struct
{
    SL32 numleft;     /* number of unused bytes left in the stream */
    UB8 *bytestream;  /* points to unused bytes in stream */
    UW16 bitbuf;      /* bit buffer normally contains 9 to 16 bits */
    UB8  bitct;       /* number of bits left in bit buf */
} BITSERVE_IM;

/* moved here from /gray/imagegr.h */
typedef struct
{
    /* Sub-pixel definition */
    SW16  numXsubpixels;
    SW16  numYsubpixels;
    SW16 maxpixval;     /* max gray value a pixel can have */
    SW16 maxphval;      /* max gray value a phase can have */
    /* Grayscale phasing */
    SW16 numXphases;
    SW16 numYphases;
    SW16 totphases;

    UW16 bsmask;     /*  buf&bsmask = the low few bits of bit stream  */
    UW16 bsct;       /*  buf>>bsshift = new buf discarding above bits */
} GRAYIMAGE;

#endif	/* GRAYSCALING */


#if BYTEORDER == LOHI
/* moved here from /psi/t1idecod.c (sharing "tmp_sw" struct definition) */
/* Swapping macros */

#define WSWAP  if_state.tmp_sw.w = *(UW16*)p; \
*p++ = if_state.tmp_sw.b.b1; \
*p++ = if_state.tmp_sw.b.b0;

#define LSWAP   \
  if_state.tmp_sw.w = *(LPUW16)p; \
  *((LPUW16)p) = *((LPUW16)(p+2)); \
  *((LPUW16)(p+2)) = if_state.tmp_sw.w; \
  WSWAP \
  WSWAP

#else
/* don't swap in this case, just bump pointer to keep up w/code -ss 10/24/91 */
#define WSWAP   p += 2;
#define LSWAP   p += 4;

#endif	/* BYTEORDER == LOHI */


#if PST1_RDR

#define NUMHINTS 96

#define T1ARGSTACKSIZE	50
#define T2ARGSTACKSIZE	50
#endif	/* PST1_RDR	*/

/* new typedefs / defines for CGIFtt_cmap_query() function */
#if (TT_ROM || TT_DISK || TT_ROM_ACT)

#define MAX_NUM_CMAP 20
	
typedef struct 
{
	UW16 platId;
	UW16 specId;
	UL32 offset;
}
CMAP_SUBTABLE;
	
typedef struct
{
	UW16 version;
	UW16 numCmap;
	CMAP_SUBTABLE subtable[MAX_NUM_CMAP]; 
}
CMAP_HEAD;

typedef struct 
{
	UW16 platId;
	UW16 specId;
}
CMAP_ENTRY;
	
typedef struct
{
	UW16 version;
	UW16 numCmap;
	CMAP_ENTRY entry[MAX_NUM_CMAP]; 
}
CMAP_QUERY;

/* new typedefs / defines for CGIFtt_name_query() function */

/* buffer size limits for TTF_NAME structs */
#define MAX_FONT_NAME_LEN			50
#define MAX_FONT_FAMILY_NAME_LEN	50
#define MAX_COPYRIGHT_LEN			200

#define MSFT_ENC           3    /* platform ID */
#define US_ENGL       0x0409    /* language ID in name table */

typedef struct {
	SB8 font_name[MAX_FONT_NAME_LEN];
	BOOLEAN font_name_too_long;
	SB8 font_family_name[MAX_FONT_FAMILY_NAME_LEN];
	BOOLEAN font_family_name_too_long;
	SB8 copyright[MAX_COPYRIGHT_LEN];
	BOOLEAN copyright_too_long;
	} NAME_QUERY;		/* abbreviated contents */

#endif	/* (TT_ROM || TT_DISK || TT_ROM_ACT) */

#if LINKED_FONT

#define MAX_FONT_NUM 10
#define GLOBAL_FONT_ID 0
#define MEMBER_FONT_ID 1

typedef struct {
	
	SL32		font_id;
	UB8			font_id_used;	/* if true, font_id above is used */
	FILECHAR	nameInSet[CFFNAMELEN];
	UL32		optionalThreshold;
	UW16		ssnum;
	LPUB8		font_hdr;		/* pointer to font header of each seperate font */
    UL32		dl_ssnum;
	UW16		user_platID;	/* user-supplied platform ID of CMAP table */
	UW16		user_specID;	/* user-supplied specific ID of CMAP table */
	UL32		ExtndFlags;		/* not all bits needed */ 
	SW16		ttc_index;	/* index of font within TTC (TrueTypeCollection) file. */
    SW16		userDesignVector[4];  /* Multi Master user design vector */
	UW16		format;       /* format flags, only certain bits needed */
	
} FONT_INFO; 


typedef struct  {
	SB8		lf[11];
	SL32	version_num;	
	UL32	ft_counter;
	FONT_INFO font_info[MAX_FONT_NUM]; 
	} LINKED_FNT;

#endif

#if DIRECT_TT_TABLE_ACCESS
#define TT_TABLE_SLOT_UNUSED	(-1)
#define TT_TABLE_BLOCK_SIZE		10

typedef struct {
	LPUB8   pFont;			/* ptr to ROM, or pathname */
	LPUB8   pTable;			/* ptr to TT table */
	MEM_HANDLE hFont;		/* handle of pathname (DISK only) */
	MEM_HANDLE hTable;		/* handle of TT table */
	UL32	size;
	UL32	tag;			/* tag of TT table */
	SW16    ttc_index;		/* index of font within TTC if applicable */
	SW16	num_refs;		/* number of open references to table or  */
							/* TT_TABLE_SLOT_UNUSED if available slot */
	BOOLEAN	buff_allocd;	/* = 1 if buffer needs to be freed, else = 0 */
} DIRECT_ACCESS_ENTRY;

typedef struct {
	/* UW16	num_entries; */
	UW16 capacity;
	MEM_HANDLE htables;
	DIRECT_ACCESS_ENTRY *tables;
} DIRECT_ACCESS_TABLES;
#endif

typedef void*  MUTEX;



typedef struct  {
    FONTCONTEXT cur_loc_fc; /* The last font context used by comp_pix() */

/* moved here from /da/bucket.c */
	MEM_HANDLE hBUCKlru;            /*  Least recently used list for BUCKETs */

/* moved here from /dep/extmem.c, intmem.c (two mutually-exclusive MLOCALs) */
	SL32   mem_avail[NP];   /* Available memory in pool    */
/* moved here from /dep/extmem.c, intmem.c (two mutually-exclusive GLOBALs) */
	SL32   mem_fund[NP];    /* Total memory in each pool   */

#if INT_MEM_MGT  /*  internal memory manager  */
/* MLOCALs moved here from intmem.c */
		MEM_HANDLE avail[NP];    /* circular linked list of free mem blocks */
		PAGE0 page0;
		MEM mem[NH];
#endif	/* INT_MEM_MGT */

#if CACHE
/* MLOCALs moved here from /dep/cache.c */
	MEM_HANDLE  hFNTlru;            /* Least recently used fonts       */
	MEM_HANDLE  hBMlru;             /* LRU character IFBITMAPs         */
#endif

#if UFST_MULTITHREAD
	UL32 client_count;				/* number of active clients ...    */
#endif

#if FCO_RDR
	/* don't free CG_SERVER until = 0  */
    MEM_HANDLE FCObject;     /* for Font Collection Object, from CGIFfont() */
    MEM_HANDLE FCO_PluginH;  /* handle of plugin FCO */
	MEM_HANDLE FCO_List[MAX_FCOS];  /* linked list of FCO handles */
#if PLUGINS
	TREETYPE    FCO_pTrees;	/* MicroType plugin tree table */
	SL32  fco_StickyPlugin;	/* If set, turn off plugin processing in fco_InitFCO() */
#endif  /* PLUGINS */
    MEM_HANDLE coordBufH;
	SL32 coordBufSize;

#endif
	UW16 MTinitstate;
} CG_SERVER;
/*---------------------------------------------------------------------*/
/*                         I F _ S T A T E                             */

typedef struct
{
/*                             S C A L E                               */
  /* was in SCALE structure */

    COORD_DATA   x;           /* x grid alignment and scaling           */
    COORD_DATA   y;           /* y grid alignment and scaling           */
    FPNUM        m[4];        /* matrix trsnsform from design to output */
    SW16VECTOR   tt;
    INTRVECTOR   xlate;

    COMP_PIX_CONTEXT   comp_pix_context;  /* 27-Jan-98 awr */
    INTRBOX  tbound;          /* bounding box in working bitmap space   */

    INTRVECTOR  p0;           /* Current point in output path           */
    OUT_TBL     out;          /* Table of current output functions      */
    PVOID       out_instance; /* Output processor instance and state    */

    PBUCKET pbucket;         /* current typeface bucket */
    CHAR_STATS cs;          /* character stats */

    FONTCONTEXT cur_loc_fc; /* The last font context used by comp_pix() */

#if IF_RDR || PST1_RDR
	MEM_HANDLE  hchar_buf;
    LPSB8   char_buf;           /* character data buffer      */
    LPSB8   expand_buf;         /* character expansion buffer */
    UW16    expand_size;        /* size of expansion buffer   */
#endif


/* GLOBAL moved here from /da/symmap.c */
	SYMBOLSET symbolset;

/* GLOBAL moved here from /dep/cgif.c */
	FONTCONTEXT fcCur;      /* current FONTCONTEXT as passed into 
                                   CGIFfont() by application */

/* GLOBALs moved here from /da/chr_def.c */
/* Character specific data (# parts, escape, is_compound, etc.) */
	CHR_DEF chr_def[MAX_CC_PARTS];
	CHR_DEF_HDR chr_def_hdr;

/* GLOBAL moved here from /da/ix.c */
	FONTINDEX fontindex;

    SL32     right_shift;
    SL32     shear;           /* detects shear; see matrix_to_scale()   */

    SW16     origBaseline;    /* Original baseline in Design units (DU) */
    SW16     aBaselnVal;      /* grid aligned baseline value (DU)       */
    UW16     bit_map_width;   /* bitmap alignment                       */
    SW16     quadrant;        /* 90 rotations: ROT0 ROT90 ROT180 ROT270 */
    UW16     quality;         /* qulaity level 1, 2 or 3                */

/* bucket-related */
    UW16    fst_type;
    UW16    usePlugins;      /* true: character set Augmentation with buckets;
                              * false: no augmentation */

/* former IF_CHAR elements which are used throughout UFST */

    UW16    num_loops;          /* number of loops */
    SW16    alt_width;          /* if>0, alternate character width */
    UW16    orThreshold;    /* Pixel size threshold above which ON */
                            /* transitions may be OR-ed to bitmap. */

/* GLOBAL moved here from /dep/cgif.c */
	UW16  CGIFinitstate;
/* GLOBAL moved here from /dep/fpmath.c */
	UW16  fpmath_error;

    BOOLEAN  or_on;           /* 1 if or on transitions, 0 otherwise    */

	/*Backed out the MPR changes. In a future if you need to put the MPR*/ 
	/*changes back, please  search for the string JWD_MPR_FIX. */  
	/*in comp_pix.c, stikoutl.c, maker.c and IF_TYPE.H files.*/

	/*BOOLEAN  save_or_on; */     /* 1 if or on transitions, 0 otherwise    */
	                          /* JWD_MPR_FIX */


    BOOLEAN  wrong_read;      /* TRUE if wrong reading                  */
    BOOLEAN  making_bold;     /* 1 if psuedo bold, 0 otherwise          */
    BOOLEAN  non_z_wind;      /* 1 if non-zero winding, 0 otherwise     */

	BOOLEAN  est_outline;

/*----------------------------------------------------------------------*/

	CG_SERVER *pserver;	/* 06-22-04 jfd ... server data shared by mt clients */
	CG_SERVER server;

#if GG_CUSTOM    /* Global Graphics memory management */
        void*  mem_context;
#endif

/*----------------------------------------------------------------------*/
#if CACHE
/* MLOCALs moved here from /dep/cache.c */
	HIFBITMAP   hBigBM;             /* buffer handle of big character  */

/* GLOBALs moved here from /dep/cache.c */
	HIFFONT     hfontCur;           /* handle of current FONT         */
	PFONT       pfontCur;           /* ptr to current FONT            */
	UW16	max_char_size;      /* max cached bitmap size          */
#if TILE
	HIFBITMAP htileBM;         /* Handle of last made tile part   */
#endif
#endif	/* CACHE */

/*----------------------------------------------------------------------*/
#if CHAR_SIZE
/* GLOBALs moved here from /dep/cgif.c */
	LPUB8    cc_buf;          /* compound character buffer        */
	SL32     cc_size;         /* size of comp. char. buffer       */
#endif

/*----------------------------------------------------------------------*/
#if (CGBITMAP)
/* formerly in "State_bm" */
    BOX            tile_box;
    SW16VECTOR     bmdim;
    SL32       nzw_size;
    MEM_HANDLE hnzw;   /* non zero winding buffer handle */
    LPUB8      nzw;
/* expand "BITMAP_BUFFERS bmb" */    /* For entire bitmap */
    MEM_HANDLE hbm;
    MEM_HANDLE horb;
    PIFBITMAP bm;
    LPUB8 orb;
/* end bmb */
    SL32            bmbufs_dyn;

#if IFBITSPLIT
    MEM_HANDLE bmdatahandle;
    UL32 *bmdataptr;
#endif

/* formerly in "SCAN_CONVERT" */
    SL32 tile_on;
    BOX tb;   /* box in output bitmap to clip to */
	IFBITMAP   entire_bm;     /* header of entire bitmap w/ metrics */

/* MLOCALs moved here from /bmp/lineto.c */
	Tran_lineto		pseudo_tran;
	State_lineto	state_lineto;
	SL32	pseudo_state;
	SL32	first_pseudo_tran;
#endif	/* CGBITMAP */

#if (CGBITMAP || GRAYSCALING)
/* GLOBAL moved here from /bmp/bitmap.c */
    RASTER ras;
#endif    /* CGBITMAP || GRAYSCALING */

#if GRAYSCALING
/* GLOBAL moved here from /gray/graymap.c */
    GRAYFILTER grayfilter;  /* global version */

/* MLOCALs moved here from /gray/graymap.c */
    BITSERVE_GM bs_gm;
    UINTG graysize[MAXGSCOUNT];
    INTG  gs_count;

/* MLOCAL moved here from /gray/imagegr.c */
    BITSERVE_IM bs_im;
#if UFST_MULTITHREAD
	int    maxpixval;
	int    maxpixval_4;
	LPUB8  gmBuff;
	int    gmBlackWid;
	LPUB8  charBuf;
	int    charBufSize;
#ifdef AGFADEBUG
	int    min_x;
	int    min_y;
	int    min_v;
	int    max_x;
	int    max_y;
	int    max_v;
#endif	/* AGFADEBUG */
#endif	/* UFST_MULTITHREAD */
#endif    /* GRAYSCALING */

/*----------------------------------------------------------------------*/
#if OUTLINE
/* GLOBAL moved here from /out/outdata.c */
	ODATA odata; /* Outline output processor */
#endif	/* OUTLINE && OLDATA */

/*----------------------------------------------------------------------*/

	UW16 font_access;	/* set by user to identify font access by */
								/* DISK (DISK_ACCESS) or ROM (ROM_ACCESS) */

/*----------------------------------------------------------------------*/
#if DISK_FONTS

/* moved here from /da/path.c */
	SB8  ufstPath[PATHNAMELEN];
	SB8  typePath[PATHNAMELEN];
/* moved here from /da/bucket.c */
	UW16 num_open_files;      /* current number of open library files */
	UW16 max_open_files;      /* maximum   "     "   "     "      "   */
    BOOLEAN  has_path;

/* MLOCALs moved here from /da/symmap.c */
	LPSB8 shortPath[NSS_FILES];
	LPUW16 symbol_set_symm[NSS_FILES];

/* GLOBALs moved here from /da/symmap.c */
#if (IF_RDR  ||  PST1_RDR)
	UW16	symbol_setIF[256];     /* current IF symbol set */
#endif
#if (TT_RDR || FCO_RDR)
	UW16	symbol_setTT[256];     /* current TT symbol set */
#endif
#if FCO_RDR
	UW16	symbol_setMT[256];     /* current MT symbol set */
#endif

#endif	/* DISK_FONTS */

/*----------------------------------------------------------------------*/
#if FCO_RDR

#if FCOACCESS
	FCACCESSTYPE   fc_acc;
	UW16     size_computed;
#endif	/* FCOACCESS */

      /* Tracks # points in char */
    UW16    fst_type_Lvl0;   /* fst type of TFS bucket */
    UB8     buck_SearchLvl;
	BOOLEAN  savenzw;      	  /* temp copy of "non_z_wind"    */




#if USBOUNDBOX					/* AJ - 10/04/04 */

    UW16    USBBOXorigScaleFactor;
    UW16    USBBOXscaleFactor;
    SL32    USBBOXxmin;     /* Unscaled Bounding box members. */
    SL32    USBBOXxmax;     /* jwd, 03/15/04.                 */
    SL32    USBBOXymin;
    SL32    USBBOXymax;
    SL32   *USBBOXXsyntell;
    SL32   *USBBOXYsyntell;
    SL32   *USBBOXIsyntell;
    SL32	USBBOXfco_TisOrthogonal;
	SL32	USBBOXfco_Tmatrix[5];
	SL32	USBBOXoutl_FuncType;
		
    SL32   *UScoordOU;
	BOOLEAN isUSBBOX; /* keb 6/1/06 */
#endif

#endif	/* FCO_RDR */

/*----------------------------------------------------------------------*/

#if PST1_RDR


/* MLOCALs moved here from /psi/t1idecod.c */ 
/*
arguments for flexproc
add 12/16/91 - rs
and all arguments read from font
change from SW16 to SL32 to handle 32 bit div
*/
	SL32     args[T1ARGSTACKSIZE];

/*  5/14/93 - rs
   The following variables were move from 'type1char()' in order to
   integrade the Pipeline upgrade. They are now shared among multiple funcs.
*/
	LPSL32  argsptr;
#if PS_ERRORCHECK
	LPSL32	t1_stacklimit;		/* points to last entry of args[] */
#endif

	SL32  curve1[6];
	SL32  curve2[6];

/* subroutine call stack */
	struct
	{
  		SW16 key;
  		LPUB8 data;
	} subcalls[11];

/* GLOBALs moved here from /psi/t1idecod.c */
	PBUCKET   gpxb; /* add 1/6/92 - rs */
	PPS_BUCK   gpps;
	PCHAR_STATS  gpcs;

	SL32 record; /* init FALSE - 4/14/93 - rs */
		

/* MLOCALs moved here from /psi/t1iscan.c */
	SL32  scanning_proc;  /* recursion of procedure scan */

/* MLOCALs moved here from /psi/t1ihints.c */
	SW16 hintssorted;
#if NON_Z_WIND
	SW16   last_pt_is_start_pt; /* 10-07-93 jfd */
#endif  

	SW16 stemhints;

/* global pointer for PST1_RAM option */
#if ROM
	UB8 *psptr;
#endif

#endif	/* PST1_RDR */

/*----------------------------------------------------------------------*/
/* misc ASIAN-conditional vbls */

#if ASIAN_ENCODING && (PST1_RDR || TT_RDR)
/* MLOCALs moved here from /da/kanmap.c */

#if UNICODE_IN
#if JIS_ENCODING
	LPSB8 UnicodeToJisTbl;
	MEM_HANDLE hUnicodeToJisTbl;  /* 9-26-96 */
	UL32 u2jSize;
#endif
#if K_ENCODING
	LPSB8 UnicodeToKscTbl;
	MEM_HANDLE hUnicodeToKscTbl;  /* 9-26-96 */
	UL32 u2kSize;
#endif
#if BIG5_ENCODING
	LPSB8 UnicodeToBig5Tbl;
	MEM_HANDLE hUnicodeToBig5Tbl;  /* 9-26-96 */
	UL32 u2big5Size;
#endif
#if GB_ENCODING
	LPSB8 UnicodeToGbTbl;
	MEM_HANDLE hUnicodeToGbTbl;  /* 9-26-96 */
	UL32 u2gbSize;
#endif
#endif /* UNICODE_IN */

#if (UNICODE_MAPPING & JIS2UNI_MAP)
	LPSB8 JisToUnicodeTbl;
	MEM_HANDLE hJisToUnicodeTbl;  /* 9-26-96 */
	UL32 j2uSize;
#endif
#if (UNICODE_MAPPING & KSC2UNI_MAP)
	LPSB8 KscToUnicodeTbl;
	MEM_HANDLE hKscToUnicodeTbl;  /* 9-26-96 */
	UL32 k2uSize;
#endif
#if (UNICODE_MAPPING & BIG52UNI_MAP)
	LPSB8 Big5ToUnicodeTbl;
	MEM_HANDLE hBig5ToUnicodeTbl;  /* 9-26-96 */
	UL32 big52uSize;
#endif
#if (UNICODE_MAPPING & GB2UNI_MAP)
	LPSB8 GbToUnicodeTbl;
	MEM_HANDLE hGbToUnicodeTbl;  /* 9-26-96 */
	UL32 gb2uSize;
#endif
#endif /* ASIAN_ENCODING && (PST1_RDR || TT_RDR) */

/* keb 8/99 */
    UW16	CharVertWrit;
    UW16  	have_adjusted_angle;  	/* 1 if we have changed things due to adjust_angle */
                                    /* being 1, 0 otherwise, 2 initially */

#if EMBEDDED_BITMAPS && TT_RDR && (CGBITMAP || GRAYSCALING)
	BOOLEAN		BitmapManipulated;		/* Requested character is to be manipulated (emboldened, sheared, etc.) */
	BOOLEAN     BitmapEmbedded;			/* Requested character exists in embedded bitmap table at req size */
#endif	/* EMBEDDED_BITMAPS && TT_RDR && (CGBITMAP || GRAYSCALING) */

/*----------------------------------------------------------------------*/
/* misc TT_*-conditional vbls */

#if TT_TTPLUG
/* TT Plugins are in TrueType format. */
	SL32         ttplug_font_type;
#if DISK_FONTS
	PATHNAME    tt_universal;
#endif
	LPUB8       tt_fnt_hdr;
#endif	/* TT_TTPLUG */


/*----------------------------------------------------------------------*/
/* MISCELLANEOUS vbls */

    VOID  *chIdptr;          /* ptr to chId (used if ss mapping is removed) */
#if PST1_RDR
    VOID  *PSchar_name;      /* ptr to PS character name ( "  "   "  "    ) */
	SL32  cidIndex;
#endif  /* PST1_RDR */


#if BOLD_HORIZONTAL
    SMEAR_INSTANCE sihorizontal;
#endif
#if BOLD_VERTICAL
    SMEAR_INSTANCE sivertical;
#endif
#if BOLD_P6
    SMEAR_INSTANCE sip6hor;
    SMEAR_INSTANCE sip6ver;
#endif
#if BOLD_P6 || BOLD_HORIZONTAL || BOLD_VERTICAL
    SW16 boldembox;  /* !!!!! 10-25-96 !!!! */
#endif


#if SLIM_FONTS || PS_IFPLUG
/* GLOBALs moved here from /da/fm.c */
	CHARWIDTH missing_ccpart_widthinfo;
	SW16      missing_ccparts;
#endif

#if (BYTEORDER == LOHI)
/* try to merge a few of the myriad swapping macros... */
	union {
       SW16 w;
       struct{
           SB8 b0;
           SB8 b1;
       }b;
    } tmp_sw;
#if PCLEO_RDR
/* MLOCALs moved here from /da/pcl_swap.c */
	UB8 even_odd;
	UB8 N_TR_pclsw;
#endif	/* PCLEO_RDR */
#endif	/* BYTEORDER == LOHI */


#if PCLEO_RDR
	UW16 PCL_font_type;	/* 0 = bound, 1 = Unbound */


    UW16 PCL_FSTTechnology; /* DL fonthdr technology field */
                                /* 1 = Scalable, 254 = bitmap. */
                                /* jwd, 07/21/03.              */

    MEM_HANDLE PCL_BitMapCharPtr; 

                                /* pntr to char returned from  */
                                /* PCLchId2ptr() jwd 07/21/03. */

    BOOLEAN DLBmpInProcess; /* Flag which indicates if PCL */
                                /* DL bitmaps are curently be- */
                                /* ing processed. 0=no, 1=yes. */
                                /* jwd 07/21/03				*/
#if MTI_PCLEO_DEMO	
	PCLEO_CALLBACK_DATA pcd;	/* data structure to support demo code & callbacks */
								/* can be replaced with customer's own data structure */
#endif

	BOOLEAN efm_font;	/* 1 = EFM font, 0 = !EFM font */
#endif	/* PCLEO_RDR */


#if PST1_SFNTI
/* MLOCALs moved here from /psi/t1isfnt.c */
	LPUB8 ph; /* this will probably go away when the real thing is done */
#endif	/* PST1_SFNTI */


#if COMPRESSED_CACHE
/* MLOCAL moved here from /dep/cmpr.c */
	BITSERV_CMPR bs_cmpr;
#endif	/* COMPRESSED_CACHE */
	
/*----------------------------------------------------------------------*/
#if (IF_RDR)

	UW16    ConnectingChar;
    LPSB8    next_coord_data;


/* was IF_CHAR structure */
    BOX     bound_box;          /* character bounding box- design units */
    BOX     escape_box;         /* character escapement bex-  "     "   */



#if SLIM_FONTS && (CGBITMAP || LINEAR || QUADRA || GRAYSCALING)
/* GLOBAL moved here from /out/quadra.c */
	SW16 special_case;
#endif

    UW16    orThresholdIF;    /* Pixel size threshold above which ON */
                              /* transitions may be OR-ed to bitmap. */

#endif	/* IF_RDR */

	SW16	lpm;
	SW16	xlpm;
	SW16	ylpm;

#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)
	BOOLEAN update_reflines;		/* TRUE if "HOxo" metrics needed for autohint_stik() */
	BOOLEAN update_reflines_flag_set;	/* TRUE if test for "HOxo metrics needed" already done */
	BOOLEAN extract_outline_only;	/* TRUE if only requesting FS_OUTLINE data from extract_outline() */ 
#if FS_DIRECT
	BOOLEAN	 direct_draw;	/* set by the process if condition met, indicating the direct draw processing is in proress */		
#endif
	FS_FIXED lo_y;			/* smallest y coordinate returned by extract_outline() */
	FS_FIXED hi_y;			/* largest y coordinate returned by extract_outline() */
	FS_FIXED cap_round;
	FS_FIXED cap_square;
	FS_FIXED x_round;
	FS_FIXED x_square;
	FS_FIXED base_round;
	FS_FIXED base_square;
	FS_FIXED lc_accent_miny;	/* 01-22-04 jfd */
	FS_FIXED uc_accent_miny;	/* 01-22-04 jfd */
#if ARABIC_AUTO_HINT
	FS_FIXED arabic_upper_accent_bot;
	FS_FIXED arabic_upper_accent_top;
	FS_FIXED arabic_upper_accent_stem_top;
	FS_FIXED arabic_lower_meduim_accent_bot;
	FS_FIXED arabic_upper_meduim_accent_bot;
	FS_FIXED arabic_upper_numeral_top;
	FS_FIXED arabic_upper_numeral_bot;
	FS_FIXED arabic_lower_numeral_top;
	FS_FIXED arabic_lower_numeral_bot;
	FS_FIXED arabic_upper_high_top;
	FS_FIXED arabic_upper_high_bot;
	FS_FIXED arabic_lower_high_bot;
	FS_FIXED arabic_lower_meduim_high_bot;
	FS_FIXED arabic_lower_meduim_short_bot;
	FS_FIXED arabic_lower_short_high_bot;
	FS_FIXED arabic_lower_short_short_bot;
	FS_FIXED arabic_center_high_top;
	FS_FIXED arabic_center_short_top;

	FS_FIXED arabic_upper_high_1_dot_top;
	FS_FIXED arabic_upper_high_1_dot_bot;
	FS_FIXED arabic_upper_high_2_dot_top;
	FS_FIXED arabic_upper_high_2_dot_bot;
	FS_FIXED arabic_upper_high_3_dot_top;
	FS_FIXED arabic_upper_high_3_dot_bot;

	FS_FIXED arabic_upper_short_1_dot_top;
	FS_FIXED arabic_upper_short_1_dot_bot;
	FS_FIXED arabic_upper_short_2_dot_top;
	FS_FIXED arabic_upper_short_2_dot_bot;
	FS_FIXED arabic_upper_short_3_dot_top;
	FS_FIXED arabic_upper_short_3_dot_bot;

	FS_FIXED arabic_pre_upper_accent_bot;
	FS_FIXED arabic_pre_lower_meduim_accent_bot;
	FS_FIXED arabic_pre_upper_meduim_accent_bot;
	FS_FIXED arabic_pre_upper_numeral_top;
	FS_FIXED arabic_pre_upper_numeral_bot;
	FS_FIXED arabic_pre_lower_numeral_top;
	FS_FIXED arabic_pre_lower_numeral_bot;
	FS_FIXED arabic_pre_upper_high_top;
	FS_FIXED arabic_pre_upper_high_bot;
	FS_FIXED arabic_pre_lower_high_bot;
	FS_FIXED arabic_pre_lower_meduim_high_bot;
	FS_FIXED arabic_pre_lower_meduim_short_bot;
	FS_FIXED arabic_pre_lower_short_high_bot;
	FS_FIXED arabic_pre_lower_short_short_bot;
	FS_FIXED arabic_pre_center_high_top;
	FS_FIXED arabic_pre_center_short_top;
	FS_FIXED arabic_pre_upper_high_dot_bot;
	FS_FIXED arabic_pre_upper_short_dot_bot;
	FS_FIXED arabic_pre_lower_high_dot_bot;
	FS_FIXED arabic_pre_lower_short_dot_bot;

	FS_FIXED arabic_subscript_kasra_bot;
	FS_FIXED arabic_subscript_kasra_top;
	FS_FIXED arabic_superscript_fatha_bot;
	FS_FIXED arabic_superscript_fatha_top;
	FS_FIXED arabic_superscript_shadda_bot;
	FS_FIXED arabic_superscript_shadda_top;
	FS_FIXED arabic_superscript_damma_bot;
	FS_FIXED arabic_superscript_damma_top;
	FS_FIXED arabic_superscript_sukun_bot;
	FS_FIXED arabic_superscript_sukun_top;
	FS_FIXED arabic_superscript_alef_bot;
	FS_FIXED arabic_superscript_alef_top;
	FS_FIXED arabic_subscript_alef_bot;
	FS_FIXED arabic_subscript_alef_top;
		

	FS_FIXED arabic_accent_shadda_kasra_bot;
	FS_FIXED arabic_accent_shadda_kasra_top;
	FS_FIXED arabic_accent_shadda_fatha_bot;
	FS_FIXED arabic_accent_shadda_fatha_top;
		

	FS_FIXED arabic_pre_lo_y;
	FS_FIXED arabic_pre_hi_y;
#endif /* ARABIC_AUTO_HINT */
#endif

#if (TT_RDR || FCO_RDR)
	BOOLEAN substitute_hollow_box;
#endif

#if LINKED_FONT
	FONTCONTEXT fcLinkedFont;
#endif
#if DIRECT_TT_TABLE_ACCESS
	MEM_HANDLE	hdatables;
	DIRECT_ACCESS_TABLES *datables;
#endif

	SW16 trace_sw;		/* enable-debug-printout flag */
#if STIK && ARABIC_AUTO_HINT	
	SW16 language_flag;
#else
	BOOLEAN language_flag; /* added 4/21/04 bjg */
#endif /*	ARABIC_AUTO_HINT */
	MUTEX mutex_ptr;	/* the mutex used for multi-threading */
	MUTEX app_mutex_ptr;/* an application mutex for multi-threading that is user-callable */
	SL32 error;

	UL32 totalValueCount;	/* number of transitions in all */
	BOOLEAN     reverseWriting; /* writing in reverse direction */
#if	1	/* 02-27-06  BUCKET-related changes */
	BOOLEAN	   useCmapTable;	 /* whether to use CMAP table to get glyph
									index - if FALSE, set glyphIndex directly */
#endif	/* 02-27-06  BUCKET-related changes */
} IF_STATE;
typedef  IF_STATE FARPTR * PIF_STATE;


#ifdef NC_REWRITE
/* a new structure for passing metrics around */
typedef struct {
	F26DOT6 lsb_x;
	F26DOT6 lsb_y;
	F26DOT6 rsb_x;
	F26DOT6 rsb_y;
	SW16 lsb;	/* unscaled */
	SW16 aw;	/* unscaled */
	BOOLEAN useMyMetrics;
	} fsg_Metrics;

#endif /* NC_REWRITE */

/*---------------------------------------------------------------------*/

#endif	/* __IF_TYPE__ */
