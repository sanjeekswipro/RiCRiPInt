/* $HopeName: GGEufst5!sys:inc:adj_skel.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/sys:inc:adj_skel.h,v 1.3.8.1.1.1 2013/12/19 11:24:04 rogerb Exp $ */
/* $Date: 2013/12/19 11:24:04 $ */
/*
*  adjust_skel.h
*
*
*
* 
*  History:
*  ---------
*    1-Nov-87    Initial Release
*   23-Jan-88    incorporated intermed in adjust_skel_type struct (tbh)
*    4-Feb-88    changed num_seg_p_loop from UWORD to WORD (tbh)
*   31-Jan-91    Changed history order, added support for MSC multi-model
*                compilation.  change "adjusted_skel_type" to uppercase
*                "ADJUSTED_SKEL" to be consistent with the rest of the code.
*    8-Jul-91    Added elasticity
*    2-Apr-92 jfd  In structure ADJUSTED_SKEL, made the following changes:
*                  1.) Added field "baseproj".
*                  2.) Removed fields "pixel_grid_line", "processing_status"
*                      and "parent".
*                  3.) Conditionally compiled fields "remainder" and 
*                      "num_breaks" based on ELASTIC*.
*                These changes are in conjunction with changes made to IF 3.0
*   02-Apr-92    Portability cleanup (BYTE, WORD, LONG, etc) (see port.h).
*   14-Nov-92 rs Port to Mac - rename italic() to cgitalic(), bold() to cgbold().
*   10-Jan-97 dlk Removed ELASTIC_X and ELASTIC_Y as part of project to
*                trim ufst.  This droppie two fields from ADJUSTED_SKEL struct.
*	27-Mar-98 slg	Move ADJUSTED_SKEL typedef into "if_type.h"
* 
*/

#ifndef __ADJ_SKEL__
#define __ADJ_SKEL__



#define  MAX_DESIGN  0x3fff    /* 16K - 1, max design unit coordinate */
#define  MAX_LOOPS    64
#define  MAX_YCLASS  256
#define  MAX_YLINES  256
#define  MAX_SKEL    96
#define ROOT            (0xff)  /*  init value: assoc's parent spec   */
#define PROCESSED       1       /*  status of skel pnt: processed     */
#define NOT_PROC        0       /*  status of skel pnt: not processed */
#define NOT_ALIGNED     -1      /*  alignment status for skel points  */

#define R_TYPES         1       /*  1 turns on "R" type functionality */
#define R_TWO_I         2       /*  "R" type index of two             */
#define YPROC           1
#define XPROC           2

typedef struct
{
    SW16 xyoff;
    SB8 spare;
    SB8 polarity;
    SW16 child;
    SW16 sibling;
}
CTREE;
typedef CTREE FARPTR * PCTREE;

/* 38 or 43 SB8s */
typedef struct
{
    UB8  num_skel;        /* number of skels         */
    LPUB8 num_skel_loop;   /*  num of skel per loop   */
    LPUW16 skel_to_contr;   /*  skel indices to contr  */
    UB8  num_trees;       /* number of trees (roots) */
    UB8  num_nodes;       /* number of tree nodes    */
    LPUB8 tree_skel_i;     /* skeletal indices        */
    LPUB8 tree_attrib;     /* skeletal attributes     */

    UB8  stan_dim_i;

/** I6.2.3   Interpolated Association data */

    UB8  num_intrp;             /* number of pairs of interp skels */
    LPUB8 intrp1_skel_i;         /* 1st interp skel indices    */
    LPUB8 intrp2_skel_i;         /* 2nd interp skel indices    */
    LPUB8 num_intrp_assoc;       /* number of interps per pair */
    UB8  tot_num_intrp_assoc;   /* number of interp assocs    */
    LPUB8 intrp_assoc_skel_i;    /* interp assoc indices       */

}
SKEL;
typedef SKEL FARPTR * PSKEL;

/* moved here from /sys/inc/adj_skel.h */
typedef struct
        {
        SW16    original;       /*  original value of skel point      */
        SW16    intermed;   /*  intermediate "original" value     */
        SW16    adjusted;       /*  current abs skel point coord valu */

        SW16    baseproj;       /*  for yskel: same as original       */
                                /*  for xskel: baseline projection    */

        UB8   round_i;        /*  "R" type index for association    */

        }  ADJUSTED_SKEL;

typedef ADJUSTED_SKEL FARPTR * PADJUSTED_SKEL;

/* moved here from /sys/inc/tr_type.h */
typedef struct
{
  /* the current transform */
    SW16  num,den;
    SW16  old0;
    SW16  new0;     /* adjusted coordinate values */
    SW16  half_den;

/*-------- above is locked for assembly language access */

    SW16  old1;
    SW16  new1;

    LPUB8 num_skel_loop;   /* Number of skeletal points in next loop */
    PADJUSTED_SKEL skel;    /*  ptr to original & adjusted values */
                            /*  of next segment end               */
    LPUW16 skel_to_contr;   /*  skel indices to contr             */

    SW16  nsk;  /* number of skels in current loop */

  /* Current character */

    SW16 end;   /* end vector number in scaling  segment */
    SW16 sct;   /* number of skels processed in current loop  */

    /* Values at the start of a loop. These values will also */
    /* be used at the end of the loop.                       */

        SW16  num0,den0;
        SW16  half_den0;
        SW16  end0;
        SW16  old00;
        SW16  new00;
} TRAN;
typedef TRAN FARPTR * PTRAN;

#if BOLD
/* moved here from /cor/if_init.c */
typedef struct
{
    UW16 loop;
    UW16 coord_index;   /* coordinate index within loop */
    UW16 x;
    UW16 y;
    UB8 xsk0, xsk1, ysk0, ysk1;
} DUP_POINTS;
typedef DUP_POINTS * PDUP_POINTS;
#define MAX_DUP_POINTS 48
#endif	/* BOLD */  



typedef struct {
    BOOLEAN char_is_if;    /* 10_feb-91 awr */

    SKEL    xskel;
    SKEL    yskel;

    SW16  baseline_offset;

  /* y-classes */
/*** I6.2.1.7   Y-class Character (local) data */
    UB8    num_yclass_ass;    /* number of y class assignments */
    LPUB8  num_root_p_ass;    /* number of roots per y class   */
    LPUB8  root_skel_i;       /* root indices in y class order */
    LPUB8  yclass_i;          /* y class indices */

/** local y classes */
    UB8    num_yclass_def;     /* number of local y class definitions */
    LPUB8  yline_high_i;       /* high y line indices */
    LPUB8  yline_low_i;        /* low y line indices */

/*** local y class definitions */
    UW16    num_ylines;         /* number of local y lines */
    LPUW16  ylines;             /* local y line values */

    UW16    HqFormat;           /* Hi-Q data format: 1 = HQ1 (tangent) */
                                /* 2 = HQ2 (contact) 3 = HQ3 (compact) */


    PCTREE  ctree;



	/* GLOBALs moved here from /cor/manipula.c */

		TRAN x_tran;
		TRAN y_tran;

/* MLOCALs moved here from /cor/if.c */
		ADJUSTED_SKEL     x_skel_if[MAX_SKEL];
		ADJUSTED_SKEL     y_skel_if[MAX_SKEL];



#if BOLD
/* GLOBAL moved here from /cor/if_init.c */
		DUP_POINTS dup_points[MAX_DUP_POINTS];
#endif  


} INTEL_DATA;

#define PINTEL_DATA INTEL_DATA *

typedef struct {
	/* was ALIGNED structure */

    SW16 value;         /* aligned value */
    SW16 grid_line;     /* grid number   */
} ALIGNED;

#define PALIGNED ALIGNED *

#if 0
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

}  ASCOORD_DATA;
typedef ASCOORD_DATA FARPTR * PASCOORD_DATA;
#endif

typedef struct {
	/* MLOCALs moved here from /cor/skeletal.c */
		UW16    pix_skel;            /*  pixel size in design units        */
#ifdef LINT_ARGS
		VOID (*pix_al_ptr)(FSPvoid SW16, PCOORD_DATA, UW16, PALIGNED ); /* ptr to pixel_align()*/
#else
		VOID (*pix_al_ptr)();   /* ptr to pixel_align() function      */
#endif
		UB8 stan_dim_option;
		SW16  curr_stan_value;
}SKEL_DATA;

#define PSKEL_DATA SKEL_DATA *






#ifdef LINT_ARGS
EXTERN VOID    yclass(FSP PBUCKET,PINTEL_DATA,PADJUSTED_SKEL);
EXTERN BOOLEAN skel_proc(FSP PBUCKET, PSKEL, PDIMENSION, PCOORD_DATA,
                                            PADJUSTED_SKEL, SW16);
EXTERN VOID    cgitalic(FSP PBUCKET, PINTEL_DATA,PADJUSTED_SKEL, PADJUSTED_SKEL);
EXTERN VOID    cgbold(FSP PINTEL_DATA, PADJUSTED_SKEL, PADJUSTED_SKEL);
EXTERN UW16   manipulate(FSP PINTEL_DATA, PADJUSTED_SKEL, PADJUSTED_SKEL);
EXTERN VOID    init_xy_tran(FSP PINTEL_DATA, PADJUSTED_SKEL, PADJUSTED_SKEL);
EXTERN UW16   if_init_char(FSP PBUCKET, PINTEL_DATA id);
#else
EXTERN VOID    yclass();
EXTERN BOOLEAN skel_proc ();
EXTERN VOID    cgitalic();
EXTERN VOID    cgbold();
EXTERN UW16   manipulate();
EXTERN VOID    init_xy_tran();
EXTERN UW16   if_init_char();
#endif

#endif	/* __ADJ_SKEL__ */


