
/* Copyright (C) 2008-2013 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* if_type.h */


#ifndef __IF_TYPE__
#define __IF_TYPE__
#include    <stdio.h>


/* The OUTL_COORD type is used for "outl_XYdata", in the MT core */
typedef SL32 OUTL_COORD;

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
#if TT_RDR
	UL32 variation_selector;
#endif
}
cacheFONT;
typedef cacheFONT * PFONT;

#if TT_RDR
#define MATCHING_VARIATION_SELECTOR(pfc, pcacf) \
	(FC_ISUVS(pfc) && ((pcacf)->variation_selector == if_state.variation_selector) ) ||	\
	(!FC_ISUVS(pfc) && ((pcacf)->variation_selector == 0) )
#endif
#endif /* CACHE */

/*** END moved from cache.h ***/

/*** START moved from bitmap.h ***/


#if NON_Z_WIND

typedef struct NZ_NODE
        {
            struct NZ_NODE * link;
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

typedef NZ_NODE * PNZ_NODE;

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
typedef NZ_INSTANCE * PNZ_INSTANCE;

#endif /* NON_Z_WIND */


typedef struct
{
    CHAR_STATS o;
    LPUB8      tran_buffer;
    LPUB8      or_buffer;
    SW16VECTOR bmdim;     /* bitmap dimensions */
#if NON_Z_WIND

    NZCOUNTER  tran_run_ct;  /* number of transition runs = number of
                              * changes in y direction in character outline
                              */
    NZ_INSTANCE nz_instance;
#endif

    UW16       polarity;
    SL32         ydir;
    SL32        tsize;       /* transition array size in bytes */
    SL32         dytran;
    SL32         yraster;
    SL32        toff;        /* offset to current raster in trans array */
    SL32         xx;          /* Bit offset into raster of transition */

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
typedef UB8 * LPCHUNK;
#define CHUNK_COUNT   1
#define CHUNK_SHIFT   3
#define CHUNK_MASK    7

#elif (RASTER_ORG == SIXTEEN_BIT_CHUNK)

typedef UW16          CHUNK;
typedef UW16 * LPCHUNK;
#define CHUNK_COUNT   2
#define CHUNK_SHIFT   4
#define CHUNK_MASK    15

#elif (RASTER_ORG == THIRTYTWO_BIT_CHUNK)

typedef UL32          CHUNK;
typedef UL32 * LPCHUNK;
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
 
#define IF_MASTERPT_SIZE 2000L  /* IF 250pts * 8 for eigths of a point */
#define XSCAN_RES        2540L  /* IF design window units              */
#define IF_EM_WIDTH    ( IF_MASTERPT_SIZE * XSCAN_RES )
                                                                                                 

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
typedef OUT_TBL * POUT_TBL;

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
typedef OUT_TBL * POUT_TBL;

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
typedef LOOP * PLOOP;


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

typedef SMEAR_INSTANCE * PSMEAR_INSTANCE;
typedef PSMEAR_INSTANCE * PPSMEAR_INSTANCE;
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
typedef COORD_DATA * PCOORD_DATA;

/* Save a comp_pix context to see what changes to next time. */
typedef struct {
   UW16       masterPt;            /* master point size    */
   UW16       xRes;
   UW16       yRes;
#if FCO_RDR
   UW16       orThreshold;         /* 10-13-94, mby */
   SB8        compositionFlag;     /* 0=IF compatible; 1=TT compatible */ 
#endif  /* FCO_RDR */
   UB8        mirflags;            /* mirror_flags   */
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
typedef CHARWIDTH * PCHARWIDTH;

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
typedef MEMHEAD * PMEMHEAD;

typedef struct
{
    MEMHEAD  nullblock[NP];    /* empty mem blocks for initialization      */
} PAGE0;

typedef struct
{
    SL32    size;        /*  size of block given to us thru CGIFfund() */
    LPSB8   ptr;         /* current address of this block */
    UW16   pool;        /* memory pool that block belongs to */
} MEM;
typedef MEM * PMEM;
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
typedef ODATA * PODATA;
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
#if MEM_TRACE
	SL32 low_water[NP];	/* low-water mark in each pool (max memory actually used) */
#endif

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
	TREETYPE    FCO_pTrees;	/* MicroType plugin tree table */
	SL32  fco_StickyPlugin;	/* If set, turn off plugin processing in fco_InitFCO() */
    MEM_HANDLE coordBufH;
	SL32 coordBufSize;

#endif
	UW16 MTinitstate;
} CG_SERVER;
/*---------------------------------------------------------------------*/
/*                         I F _ S T A T E                             */
typedef struct
{

    SL32VECTOR lsb;
	SL32VECTOR aw;
	BBoxRes bbox;
} GlyphMetrics_DU_GET;

#ifdef FS_EDGE_TECH
/**
 *-----------------------------------------------------------------------------------
 *    ADFRenderAttrs (set by the application):
 *
 *    penX, penY: The x and y coordinates of the pen position, which determines the 
 *    placement of the ADF glyph origin. penX and penY are specified in floating 
 *    point pixel coordinates.
 *    
 *    pointSize: The requested point size for the ADF glyph.
 *
 *    dpi: The dots-per-inch of the display device.
 *
 *    scaleX, scaleY: Additional x and y scale factors for scaling the ADF glyph 
 *    beyond the requested point size. If the ADF glyph is an explicit ADF, non-uniform 
 *    scaling (i.e., where scaleX does not equal scaleY) distorts the distance field of 
 *    the ADF glyph, which can result in poor quality antialiasing. If the ADF glyph is 
 *    an implicit ADF, non-uniform scaling has no negative impact on the quality of the 
 *    antialiasing.
 *    
 *    rotationPtX, rotationPtY: The x and y coordinates of the center of rotation 
 *    for the ADF glyph. rotationPtX and rotationPtY are specified in floating point
 *    pixel coordinates.
 *
 *    rotationAngle: The rotation angle applied to the ADF glyph. rotationAngle is 
 *    specified in radians. Note that grid fitting is automatically disabled when a
 *    non-zero rotationAngle is specified.
 *
 *    displayMode: ADF_REND_MODE_CRT, ADF_REND_MODE_RGBv, ADF_REND_MODE_BGRv, 
 *    ADF_REND_MODE_RGBh, or ADF_REND_MODE_BGRh. This element determines whether 
 *    the ADF glyph is rendered in CRT mode (ADF_REND_MODE_CRT) or LCD mode (all
 *    others). If an LCD mode is chosen, displayMode also specifies the physical 
 *    layout of the display's sub-pixels, i.e., whether the display is horizontally
 *    or vertically striped, and whether the striping is in RGB or BGR order. This 
 *    library assumes that the striped sub-pixels have identical dimensions.
 *
 *    gridFitType: ADF_GRID_FIT_NONE, ADF_GRID_FIT_PIXEL, ADF_GRID_FIT_SUB_PIXEL, or
 *    ADF_GRID_FIT_BAZ_PIXEL, ADF_GRID_FIT_MAZ_PIXEL. ADF_GRID_FIT_NONE disables grid 
 *    fitting. ADF_GRID_FIT_PIXEL and ADF_GRID_FIT_PIXEL are not used by iType.
 *    ADF_GRID_FIT_MAZ_PIXEL aligns a glyph to the pixel grid using MAZ alignment zones.
 *    ADF_GRID_FIT_BAZ_PIXEL aligns a glyph to the pixel grid using BAZ alignment zones.
 *
 *    outsideCutoff, insideCutoff: The filter cutoff values for the mapping function
 *    which maps ADF distances to density values as described above in Usage Note:
 *    Continuous Stroke Modulation. outsideCutoff and insideCutoff are specified in
 *    floating point pixel coordinates.
 *
 *    gamma: The exponent of the gamma curve mapping ADF distances to density values 
 *    as described above in Usage Note: Continuous Stroke Modulation.
 *
 *    useColorReduction: The Boolean flag that enables color reduction during LCD
 *    rendering. Setting this variable to true enables color reduction. Setting this
 *    variable to false disables color reduction. This variable is ignored during CRT
 *    rendering (i.e., when displayMode is set to ADF_REND_MODE_CRT).
 *
 *    colorReductionAmt: If useColorReduction is set to true, this variable controls
 *    the amount of color reduction applied during LCD rendering. The value must lie in
 *    the range [0, 1]. A value of 0 specifies minimum color reduction. A value of 1
 *    specifies maximum color reduction: pixels will be completely desaturated (i.e.,
 *    no color at all). Note that a value of 0 will still cause some amount of color
 *    reduction to be performed; it will NOT give the same results as turning off color
 *    reduction entirely (i.e., by setting useColorReduction to false). This variable
 *    is ignored if useColorReduction is set to false. This variable is also ignored if 
 *    displayMode is set to ADF_REND_MODE_CRT.
 *-----------------------------------------------------------------------------------
 */
typedef struct {
    ADF_F32        penX;               /* x-coordinate of ADF glyph origin  */
    ADF_F32        penY;               /* y-coordinate of ADF glyph origin */
    ADF_F32        pointSize;          /* Requested point size */
    ADF_U32        dpi;                /* Dots-per-inch of the display device */
    ADF_F32        scaleX;             /* Additional ADF glyph scaling in x */
    ADF_F32        scaleY;             /* Additional ADF glyph scaling in y */
    ADF_F32        rotationPtX;        /* x-coord of center of rotation for ADF glyph */
    ADF_F32        rotationPtY;        /* y-coord of center of rotation for ADF glyph */
    ADF_F32        rotationAngle;      /* Rotation angle in radians */
    ADF_U32        displayMode;        /* ADF_REND_MODE_CRT, ADF_REND_MODE_RGBv, etc. */
    ADF_U32        gridFitType;        /* ADF_GRID_FIT_NONE, ADF_GRID_FIT_PIXEL, etc. */
    ADF_F32        outsideCutoff;      /* Outside filter cutoff value for CSM */
    ADF_F32        insideCutoff;       /* Inside filter cutoff value for CSM */
    ADF_F32        gamma;              /* Gamma curve exponent for CSM  */
    ADF_U32        useColorReduction;  /* Boolean that turns on/off color reduction */
    ADF_F32        colorReductionAmt;  /* Controls the amount of color reduction */
}    ADFRenderAttrs;
#endif /* FS_EDGE_TECH */

#if TT_TYPE42I && MTI_TYPE42_DEMO

/***************************/
/* Type 42 data structures */
/***************************/
#define OFFSET_TO_INDEXTOLOCFORMAT	50

typedef struct
{
    LPUB8 charp;         /*  location of Type 42 character data        */
    INTG  charcount;     /*  Byte count of data                        */
    /* INTG */ UL32 glyphID;       /*  charstrings glyph index                   */
	/* INTG */ UL32 char_code;     /*  encoding array character code             */
	FILECHAR *name;      /*  glyph name                                */
} TYPE42_CH_DIR_ENTRY;
typedef TYPE42_CH_DIR_ENTRY * PTYPE42_CH_DIR_ENTRY;

typedef struct {
     LPUB8              loc;    /* pointer to font data                */
	 INTG				nChars;	/* number of characters in Type42 font */
	 LPUB8				fontname;	/* type 42 font name               */
     PTYPE42_CH_DIR_ENTRY char_dir; /* pointer to font's char list     */
} TYPE42_HDR_DIR_ENTRY;
typedef TYPE42_HDR_DIR_ENTRY * PTYPE42_HDR_DIR_ENTRY;

typedef struct {
	/* "*charDirectory" is essentially a listing of characters stored in */
	/* the downloaded font (sequence #).                                 */
	TYPE42_HDR_DIR_ENTRY *cur_hdr;
	TYPE42_HDR_DIR_ENTRY *hdr_dir;
	INTG                numFonts;				
} TYPE42_CALLBACK_DATA;

#endif	/* TT_TYPE42I && MTI_TYPE42_DEMO */

#if SCALE_MATRIX && MATCH_SCALE_MATRIX_OUTPUT
typedef struct
{
#if (SCALE_MATRIX & MAT0_SCALE_MATRIX)
	FPNUM mat0_mfp[4];       /* Design space to output space as FP         */
#endif
#if (SCALE_MATRIX & MAT1_SCALE_MATRIX)
	FPNUM mat1_mfp[4];       /* matrix from world space to output space as FP */
	FPNUM mat1_em_widthfp;   /* size of em box in world units as FP     */
	FPNUM mat1_em_depthfp;
#endif
#if (SCALE_MATRIX & MAT2_SCALE_MATRIX)
	FPNUM mat2_mfp[4];       /* Design space to output space as FP         */
#endif
} SCALE_MATRIX_FP_M;
#endif
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
    SL32            bmbufs_dyn;

#if IFBITSPLIT
    MEM_HANDLE bmdatahandle;
    UL32 *bmdataptr;
#endif

    SL32 tile_on;
    BOX tb;   /* box in output bitmap to clip to */

/* MLOCALs moved here from /bmp/lineto.c */
	Tran_lineto		pseudo_tran;
	State_lineto	state_lineto;
	SL32	pseudo_state;
	SL32	first_pseudo_tran;
#endif	/* CGBITMAP */

#if (CGBITMAP || GRAYSCALING)
    RASTER ras;

	/* moved to "or GRAYSCALING" section to support !CGBITMAP build */
    BOX            tile_box;
    SW16VECTOR     bmdim;
    SL32       nzw_size;
    MEM_HANDLE hnzw;   /* non zero winding buffer handle */
    LPUB8      nzw;
    MEM_HANDLE hbm;
    MEM_HANDLE horb;
    PIFBITMAP bm;
    LPUB8 orb;

	IFBITMAP   entire_bm;     /* header of entire bitmap w/ metrics */
	/* end !CGBITMAP support section */
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
	SL32   maxpixval;
	SL32   maxpixval_4;
	LPUB8  gmBuff;
	SL32   gmBlackWid;
	LPUB8  charBuf;
	SL32   charBufSize;
#ifdef AGFADEBUG
	SL32    min_x;
	SL32    min_y;
	SL32    min_v;
	SL32    max_x;
	SL32    max_y;
	SL32    max_v;
#endif	/* AGFADEBUG */
	UL32    uncompressed_graymap_size;
	INTG    pixeldepth;
	LPUB8   uncompressed_graymap_buffer;
#endif    /* GRAYSCALING */
#if MEM_TRACE
	/* used to verify that NZW buffer does not overflow */
	UB8* max_nzw_addr;
#endif

/*----------------------------------------------------------------------*/
#if OUTLINE
/* GLOBAL moved here from /out/outdata.c */
	ODATA odata; /* Outline output processor */
#endif	/* OUTLINE && OLDATA */

/*----------------------------------------------------------------------*/

	BOOLEAN font_access;	/* set by user to identify font access by */
								/* DISK (DISK_ACCESS=0) or ROM (ROM_ACCESS=1) */

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

    SL32 created_by_ps;        /* used for Type 42 processing in TT code */

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
#if JOHAB_ENCODING
	LPSB8 UnicodeToJohabTbl;
	MEM_HANDLE hUnicodeToJohabTbl;  
	UL32 u2johabSize;
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
#if (UNICODE_MAPPING & JOHAB2UNI_MAP)
	LPSB8 JohabToUnicodeTbl;
	MEM_HANDLE hJohabToUnicodeTbl;  
	UL32 johab2uSize;
#endif
#endif /* ASIAN_ENCODING && (PST1_RDR || TT_RDR) */

/* keb 8/99 */
    UW16	CharVertWrit;
    UW16  	have_adjusted_angle;  	/* 1 if we have changed things due to adjust_angle */
                                    /* being 1, 0 otherwise, 2 initially */

#if EMBEDDED_BITMAPS && TT_RDR && (CGBITMAP || GRAYSCALING)
	BOOLEAN		BitmapManipulated;		/* Requested character is to be manipulated (emboldened, sheared, etc.) */
	BOOLEAN     BitmapEmbedded;			/* Requested character exists in embedded bitmap table at req size */
	BOOLEAN     BoldBitmapEmbedded;		/* Requested bold character exists in embedded bitmap table at req size */
	SL32		embed_size;				/* actual allocation to use for UFST output buffer for embedded bitmap */
	UL32		embed_chpl;				/* computation moved upstream from encode_UFST_bitmap */
#endif	/* EMBEDDED_BITMAPS && TT_RDR && (CGBITMAP || GRAYSCALING) */

/*----------------------------------------------------------------------*/
/* misc TT_*-conditional vbls */

#if TT_TTPLUG && !FCO_RDR
/* TT Plugins are in TrueType format. */
	SL32         ttplug_font_type;
#if DISK_FONTS
	PATHNAME    tt_universal;
#endif
	LPUB8       tt_fnt_hdr;
#endif	/* TT_TTPLUG && !FCO_RDR */


/*----------------------------------------------------------------------*/
/* MISCELLANEOUS vbls */

    VOID  *chIdptr;          /* ptr to chId (used if ss mapping is removed) */
	UL32  chIdnum;           /* value of chId (used if ss mapping is removed) */
#if PST1_RDR
    VOID  *PSchar_name;      /* ptr to PS character name ( "  "   "  "    ) */
#endif  /* PST1_RDR */


#if BOLD_HORIZONTAL
    SMEAR_INSTANCE sihorizontal;
#if QE_BOLD_TEST
	UW16 bold_fractional;
#endif
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


#if IF_RDR || PS_IFPLUG
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
	LPUB8 ph;
#endif	/* PST1_SFNTI */

#if TT_TYPE42I && MTI_TYPE42_DEMO
	TYPE42_CALLBACK_DATA t42d;
#endif

/*----------------------------------------------------------------------*/
#if (IF_RDR)

	UW16    ConnectingChar;
    LPSB8    next_coord_data;


/* was IF_CHAR structure */
    BOX     bound_box;          /* character bounding box- design units */
    BOX     escape_box;         /* character escapement bex-  "     "   */



#if (CGBITMAP || LINEAR || QUADRA || GRAYSCALING)
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
	BOOLEAN extract_outline_only;	/* TRUE if only requesting FS_OUTLINE data from extract_outline() */ 
	BOOLEAN	 direct_draw;	/* set if necessary conditions met for direct draw processing (for current character) */		
	FS_FIXED lo_y;			/* smallest y coordinate returned by extract_outline() */
	FS_FIXED hi_y;			/* largest y coordinate returned by extract_outline() */
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
#if TT_RDR
	UL32 variation_selector;
#endif
	SW16 trace_sw;		/* enable-debug-printout flag */
	MUTEX mutex_ptr;	/* the mutex used for multi-threading */
	MUTEX app_mutex_ptr;/* an application mutex for multi-threading that is user-callable */
	SL32 error;

	UL32 totalValueCount;	/* number of transitions in all */
	BOOLEAN     reverseWriting; /* writing in reverse direction */
    GlyphMetrics_DU_GET glyphMetricsDU; 
	BOOLEAN getPsDesignMetricsCalled;
	FILE *outStik;
#ifdef FS_EDGE_TECH
    void*  libInst;                 /* ADF instance data structure                   */
	MEM_HANDLE libInstH;			/* ADF instance data structure handle            */

    ADF_U32        adfGridFitType;  /* current Edge(TM) grid fit type                */
	ADFRenderAttrs adfRenderAttrs;
	FS_ULONG adfFlags;
	FS_ULONG adfError;
	FS_ULONG fontflags;
#endif	/* defined(FS_EDGE_TECH) */

#if SCALE_MATRIX && MATCH_SCALE_MATRIX_OUTPUT
	SCALE_MATRIX_FP_M mfp;
#endif
} IF_STATE;
typedef  IF_STATE * PIF_STATE;
typedef  GlyphMetrics_DU_GET * PGlyphMetrics_DU_GET; 

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



/*---------------------------------------------------------------------*/

#endif	/* __IF_TYPE__ */
