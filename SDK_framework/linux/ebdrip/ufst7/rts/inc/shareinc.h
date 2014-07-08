
/* Copyright (C) 2013 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* shareinc.h */


/***********************************************************************
	"shareinc.h":
	This is the sequence of include files that is required for all files
	that reference "if_state"
************************************************************************/

/*-------------------------------------------------*/
/* 
   FSP = Font State Parameter/Prototype
   FSA = Font State Argument/Access
   
   FSP0 = used to declare IF_STATE-pointer parm in function with no parameters
   FSP = " " in function with one or more parameters
   FSA0 = used to pass ptr to instance of IF_STATE to function with no arguments
   FSA = " " to function with one or more arguments

   FSPvoid, FSAvoid, FSPusevoid = used to pass IF_STATE to functions that are 
		elements of OUT_TBL - this special-case is required to avoid 
		circular-reference problem within if_type.h.
*/

#ifndef __SHAREINC__
#define __SHAREINC__

#if UFST_REENTRANT
#define FSP0	PIF_STATE pIFS
#define FSP		PIF_STATE pIFS,
#define FSPvoid	PVOID pIFSvoid,
#define FSPusevoid PIF_STATE pIFS = (PIF_STATE) pIFSvoid;
#define FSAvoid	(PVOID) pIFS,
#define FSA0	pIFS
#define FSA		pIFS,
#else
#define FSP0	VOID
#define FSP
#define FSPvoid 
#define FSPusevoid 
#define FSAvoid
#define FSA0
#define FSA
#endif

#if FS_EDGE_HINTS || FS_EDGE_RENDER
#if UFST_REENTRANT
#define STATE (*pIFS)           /* dereference passed pointer */
#define _DS0_	PIF_STATE pIFS
#define _DS_	PIF_STATE pIFS,
#define _PS0_	pIFS
#define _PS_	pIFS,
#else
#define STATE   if_state        /* dereference passed pointer */
#define _DS0_	VOID
#define _DS_
#define _PS0_
#define _PS_
#endif

/* The following are significant only if FS_MULTI_PROCESS is enabled (which it will never be) */
#define MP_memOFF(a) a
#define MP_OFF(a) a
#define MPsp_OFF(a) a
#define MPgs_OFF(a) a
#define MP_memPTR(type,a) a    
#define MP_PTR(type,a) a
#define MPsp_PTR(type,a) a
#define MPgs_PTR(type,a) a

#endif	/* FS_EDGE_HINTS || FS_EDGE_RENDER */

/*-------------------------------------------------*/
					
#include "ifmem.h"
#include "cgif.h"
#include "ix.h"

#if GRAYSCALING
#include "graymap.h"
#endif

#include "sym.h"
#include "kanji.h"

#if FCO_RDR
#include "fc_dafil.h"
#include "fc_syntl.h"
#include "fc_da.h"
#include "fc_intfl.h"
#endif

#if PCLEO_RDR
#include "pcleo.h"
#if MTI_PCLEO_DEMO	
#include "pcleomgt.h"	/* code to support demo code & callbacks */
#endif
#endif

#include "if_type.h"
#include "adj_skel.h"

#if TT_RDR
#include "fscdefs.h"
#include "fontscal.h"
#include "fontmath.h"
#include "sfnt.h"
#include "fnt.h"
#include "fsglue.h"
#endif

#include "mixmodel.h"
#include "dbg_ufst.h"
#include "cgmacros.h"

/*-------------------------------------------------*/
/* 
If UFST_REENTRANT is set, this define will turn all references of the form
"if_state.whatever" into "(*pIFS).whatever", thus using the IF_STATE-pointer
parameter that's passed around by FSP, FSP0, FSA, FSA0, etc.

If UFST_REENTRANT is not set, this define will simply assume a globally-
declared structure "if_state".

The actual GLOBAL declaration of "if_state", in /rts/gen/maker.c, is now 
enabled only if UFST_REENTRANT is 0; otherwise, the application must 
declare one or more instances of IF_STATE, and pass a pointer to one of these
instances of IF_STATE whenever it invokes any CGIF_*() function.

*/
#if UFST_REENTRANT
#define if_state (*pIFS)
#else
EXTERN IF_STATE if_state;
#endif


/*-------------------------------------------------*/
/* 
	EXTERNAL INTERFACES

All functions which can be called by an application should
be declared as UFST_EXTERNAL_INTERFACE, so that the
function can be conditionally compiled as a static-linked library or
a DLL library.

-------------------------------------------------*/

#if (UFST_LINK_MODE ==  UFST_BUILD_DLL)
/* building UFST as a DLL */
#define UFST_EXTERNAL_INTERFACE _declspec (dllexport)
#else	/* UFST_LINK_MODE ==  UFST_STATIC_LINK */
/* either building UFST as a static library,  */
/* or building a UFST application to use UFST as a static library */
#define UFST_EXTERNAL_INTERFACE EXTERN
#endif

/*-------------------------------------------------*/

#if ((TT_RDR || FCO_RDR) && (CGBITMAP || LINEAR || SMEAR_BOLD || GRAYSCALING))
#ifdef LINT_ARGS
EXTERN UW16   quadvectorize(FSP PVOID s, UW16 (*lineto)(FSPvoid PVOID, INTR, INTR),
                     INTR p0x, INTR p0y, INTR cx, INTR cy, INTR px, INTR py);
#else
EXTERN UW16   quadvectorize();
#endif
#endif	/* (TT_RDR || FCO_RDR) && (CGBITMAP || LINEAR || SMEAR_BOLD || GRAYSCALING) */

#if ((PST1_RDR) && (CGBITMAP || LINEAR || SMEAR_BOLD || GRAYSCALING))
#ifdef LINT_ARGS
EXTERN UW16  cubevectorize(FSP PVOID s, UW16 (*lineto)(FSPvoid PVOID, INTR, INTR),
                     INTR p0x, INTR p0y, INTR p1x, INTR p1y,
                     INTR p2x, INTR p2y, INTR p3x, INTR p3y);
#else
EXTERN UW16   cubevectorize();
#endif
#endif	/* (PST1_RDR) && (CGBITMAP || LINEAR || SMEAR_BOLD || GRAYSCALING) */

#if IF_RDR
#if !defined(_AM29K)
#define MDES2BM(v) \
    ( (!if_state.quadrant) /* arbitrary rotation */  \
      ?  des2bm(FSA v)                                   \
      :  cg_scale(FSA v) )
#endif
#endif	/* IF_RDR */

/*------ start of very long LINT_ARGS set of declarations -----*/
#ifdef LINT_ARGS
typedef struct {
    UW16 (*load_font) (FSP PFONT_DES, PBUCKET);
    UW16 (*unload_font) (FSP PBUCKET);
    UW16 (*set_trans) (FSP PBUCKET, SL32, SL32, SL32, SL32);
    UW16 (*set_char) (FSP PBUCKET, UL32);
    UW16 (*make_gaso_and_stats) (FSP PBUCKET, PCHAR_STATS);
    UW16 (*render) (FSP PVOID, PBUCKET, INTR, INTR);
#if WIDTHS
    UW16 (*get_width) (FSP PBUCKET);
#endif
} IF_FUNC_TBL;
typedef IF_FUNC_TBL * PIF_FUNC_TBL;


#if IF_RDR
/*
pointer callable UFST functions
*/
EXTERN UW16 ifload_font (FSP PFONT_DES, PBUCKET);
EXTERN UW16 ifunload_font (FSP PBUCKET);
EXTERN UW16 ifset_trans (FSP PBUCKET, SL32, SL32, SL32, SL32);
EXTERN UW16 ifset_char (FSP PBUCKET, UL32);
EXTERN UW16 ifmake_gaso_and_stats (FSP PBUCKET, PCHAR_STATS);
EXTERN UW16 ifrender (FSP PVOID, PBUCKET, INTR, INTR);
#if WIDTHS
EXTERN UW16 ifget_width (FSP PBUCKET);
#endif

#if FONTBBOX
EXTERN UW16 ifget_FontBBox( FSP PBUCKET, BBoxRes* );
#endif

#if FNT_METRICS
EXTERN UW16 if_get_FontMetrics( FSP PBUCKET, FONT_METRICS*, SL32 );
#endif

EXTERN VOID       first_loop (FSP0);
EXTERN VOID       next_loop (FSP PLOOP);
EXTERN VOID       pixel_align (FSPvoid SW16, PCOORD_DATA, UW16,PALIGNED);
EXTERN VOID       sd_pixel_align (FSPvoid SW16, PCOORD_DATA, UW16,PALIGNED);
EXTERN UW16       intel_char(FSP PBUCKET);
EXTERN SW16VECTOR  des2bm(FSP SW16VECTOR);
EXTERN SW16VECTOR  inv_des2bm(FSP SW16VECTOR);
EXTERN LPSB8	fgseg(FSP PBUCKET);
EXTERN UW16       LIBmake_cd(FSP PBUCKET, UW16, PCHR_DEF, SL32);

#if FONTBBOX && IF_PCLEOI
EXTERN LPUB8 PCLEO_charptr(FSP LPUB8, UW16);
#endif

#endif  /* IF_RDR */


#if PST1_RDR
/*
pointer callable UFST functions
*/
EXTERN UW16 psload_font (FSP PFONT_DES, PBUCKET);
EXTERN UW16 psunload_font (FSP PBUCKET);
EXTERN UW16 psset_trans (FSP PBUCKET, SL32, SL32, SL32, SL32);
EXTERN UW16 psset_char (FSP PBUCKET, UL32);
EXTERN UW16 psmake_gaso_and_stats (FSP PBUCKET, PCHAR_STATS);
EXTERN UW16 psrender (FSP PVOID, PBUCKET, INTR, INTR);
#if WIDTHS
EXTERN UW16 psget_width (FSP PBUCKET);
#endif

#if FNT_METRICS
EXTERN UW16 ps_get_FontMetrics( FSP PBUCKET, FONT_METRICS*, SL32 );
#endif

#if CFF_RDR
EXTERN UW16   StdStrToIndex(LPSB8);
#endif

EXTERN PVOID  IndexToCgid(UW16);
EXTERN UW16   cm_cgidToIndex(UW16);
EXTERN UW16   psnameToIndex(LPSB8);
EXTERN VOID transfn8 (FSPvoid SL32, SL32, LPSL32, LPSL32);
EXTERN VOID free_charstrings (FSP PPS_BUCK);
EXTERN VOID free_subrs (FSP PPS_BUCK);
EXTERN SL32 inithints(FSP0);
EXTERN VOID sort_hints(FSP0);
EXTERN BOOLEAN ps_is_same(FSP PBUCKET);

#endif /* PST1_RDR */


#if TT_RDR

#if (TT_ROM || TT_TYPE42I)
GLOBAL SL32  ttload_ROMSfnt( FSP PBUCKET );
#endif

#if (TT_ROM || TT_DISK || TT_ROM_ACT)
#if STIK
EXTERN FS_OUTLINE * extract_outline(FSP fsg_SplineKey *);
EXTERN UB8 get_nstk_bit(FSP UW16);
EXTERN FS_OUTLINE * expand_stik(FSP fsg_SplineKey *, FS_OUTLINE *);
EXTERN UW16 do_auto_hint(FSP fsg_SplineKey *, FS_OUTLINE *);
#endif	/* STIK */

#if (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS
EXTERN UW16 ttTBLinit (FSP0);
EXTERN VOID ttTBLexit (FSP0);
EXTERN UL32 tt_GetTagOffset(FSP LPUB8, UL32, UL32);
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_slot (FSP0);
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_font (FSP LPUB8, UL32, UW16);
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_table (FSP LPUB8);
#endif	/* (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS */

EXTERN BOOLEAN ttfont_supported(FSP VOID *);
EXTERN BOOLEAN TableOffsetsAreValid(FSP sfnt_DirectoryEntry *, SL32);
EXTERN BOOLEAN RequiredTablesExist(FSP sfnt_DirectoryEntry *, SL32);
#endif	/* (TT_ROM || TT_DISK || TT_ROM_ACT) */

/*
pointer callable UFST functions
*/
EXTERN UW16 ttload_font (FSP PFONT_DES, PBUCKET);
EXTERN UW16 ttunload_font (FSP PBUCKET);
EXTERN UW16 ttset_trans (FSP PBUCKET, SL32, SL32, SL32, SL32);
EXTERN UW16 ttset_char (FSP PBUCKET, UL32);
EXTERN UW16 ttmake_gaso_and_stats (FSP PBUCKET, PCHAR_STATS);
EXTERN UW16 ttrender (FSP PVOID, PBUCKET, INTR, INTR);
#if WIDTHS
EXTERN UW16 ttget_width (FSP PBUCKET);
#endif

#if FONTBBOX
EXTERN UW16 ttget_FontBBox( PBUCKET, BBoxRes* );
#endif

#if FNT_METRICS
EXTERN UW16 tt_get_FontMetrics( FSP PBUCKET, FONT_METRICS*, SL32 );
#endif

EXTERN VOID*  tt_GetFragment (FSP PBUCKET pBucket, SL32 offset, SL32 length);
EXTERN VOID set_plat_spec_lang(FSP UW16 *plat, UW16 *spec, UW16 *lang, UW16* cmap_ptr, UW16* name_ptr);
EXTERN UW16  ttkanset_char (FSP PBUCKET b, UL32 chId, fs_GlyphInputType *ttInput);
EXTERN UL32 tt_GetTagSize( FSP LPUB8, UL32, UL32);
EXTERN UW16 tt_GetTagBuffer( FSP LPUB8, UL32, UL32, UL32, LPUB8 );
EXTERN SW16 verifytruetype(FSP truetype_type *, UL32);

#if FS_EDGE_DUMP_OUTLINE
VOID dump_EDGE_outline(fnt_ElementType *, FILECHAR *);
VOID dump_final_EDGE_outline(fnt_ElementType *, FILECHAR *);
#endif

#endif /* TT_RDR */


#if FCO_RDR
/*
pointer callable UFST functions
*/
EXTERN UW16 fco_load_font( FSP PFONT_DES, PBUCKET );
EXTERN UW16 fco_unload_font( FSP PBUCKET );
EXTERN UW16 fco_set_trans( FSP PBUCKET, SL32, SL32, SL32, SL32 );
EXTERN UW16 fco_set_char( FSP PBUCKET, UL32 );
EXTERN UW16 fco_make_gaso_and_stats( FSP PBUCKET, PCHAR_STATS );
EXTERN UW16 fco_render( FSP PVOID, PBUCKET, INTR, INTR );
#if WIDTHS
EXTERN UW16 fco_get_width( FSP PBUCKET );
#endif

#if FONTBBOX
EXTERN UW16 fco_get_FontBBox( FSP PBUCKET, SL32, BBoxRes* );
#endif
#if FNT_METRICS
EXTERN UW16 fco_get_FontMetrics( FSP PBUCKET, FONT_METRICS*, SL32 );
#endif
#if FCO2
EXTERN UW16 decode_stream ( FSP MODELTYPE*, SL32*,SL32*, SL32*);
#endif

EXTERN VOID fco_searchPathCur( FSP SL32*, FCTYPE**, SL32[] );
#endif /* FCO_RDR */

#if (IF_RDR || PST1_RDR)
EXTERN UW16  fco_map_cgnum_to_unicode( FSP UW16 );
#endif

#if CACHE || CHAR_HANDLE 
EXTERN UW16    makechar(FSP PFONTCONTEXT, SL32, PHIFBITMAP);
#endif

#if CHAR_SIZE
EXTERN UW16    MAKfontSize(FSP PFONTCONTEXT, SL32, LPSL32);
#endif

#if NON_Z_WIND
EXTERN VOID nz_init ( FSP PNZ_INSTANCE, LPUB8, NZCOUNTER, SW16 );
EXTERN VOID nz_open_run ( FSP PNZ_INSTANCE, NZCOUNTER, SL32 );
EXTERN VOID nz_close_run ( FSP PNZ_INSTANCE,  SL32, BOOLEAN );
EXTERN UW16 nz_set_trans( FSP PNZ_INSTANCE );
EXTERN UW16 checkContours( FSP PNZ_INSTANCE nz );
#endif

#if OUTLINE
EXTERN VOID    out_setRender(FSP0);
#if OLDATA
EXTERN UW16  outbuffer(FSP PIFOUTLINE);
#endif
#endif

#if TILE  
EXTERN UW16    bitmap_tile(FSP PIFTILE, PHIFBITMAP);
#endif

#if DISK_FONTS
EXTERN UW16      IXopen_file(FSP PINDEX_ENTRY, PBUCKET);
EXTERN VOID      IXclose_file(FSP PBUCKET);
EXTERN LPSB8     FIpath(FSP PINDEX_ENTRY);
#endif

#if LINKED_FONT
EXTERN UW16 LFNT_SetFONTCONTEXT(FSP PFONTCONTEXT fc, UL32 fontNumber);
EXTERN UW16 LFNT_MAKifbmheader(FSP PFONTCONTEXT, SL32, PIFBITMAP);
#endif

#if UFST_MULTITHREAD
EXTERN UW16 remove_thread_id_from_list(FSP LPUL32);
EXTERN UW16 add_thread_id_to_list(FSP LPUL32);
EXTERN VOID print_bucket_status(FSP0);
EXTERN BOOLEAN thread_list_is_empty(FSP LPUL32);
EXTERN BOOLEAN only_thread_in_list(FSP LPUL32, UL32);
EXTERN VOID print_current_thread(FSP0);
#endif	/* UFST_MULTITHREAD */

#if FIX_CONTOURS
EXTERN UW16  fix_contours( FSP SL32 );
#endif

#if XPS_SIDEWAYS
EXTERN UW16 calc_XPS_attributes(FSP PVOID);
#endif

#if BOLD_P6 || BOLD_HORIZONTAL || BOLD_VERTICAL 
EXTERN VOID smear_hookRender( PSMEAR_INSTANCE, PPVOID, POUT_TBL );
EXTERN UW16	smear_calcvec( FSP UW16, PSL32VECTOR, PSL32VECTOR, FILECHAR * );
#endif

#if BOLD_HORIZONTAL
EXTERN UW16   smear_setboldhorizontal ( FSP UW16, PSMEAR_INSTANCE );
#endif

#if BOLD_VERTICAL
EXTERN UW16   smear_setboldvertical ( FSP UW16, PSMEAR_INSTANCE );
#endif

#if BOLD_P6
EXTERN UW16   smear_setboldp6 ( FSP UW16, PSMEAR_INSTANCE, PSMEAR_INSTANCE );
#endif

#if BOLD_CJK
EXTERN FS_BITMAP *pixelbold_bitmap(FSP FS_BITMAP *);
EXTERN VOID bmap_to_bmap(FSP FS_BITMAP *, PIFBITMAP);

#if GRAYSCALING
EXTERN UL32 uncompressed_graymap_size( FSP GRAYFILTER *);
EXTERN FS_GRAYMAP *pixelbold_graymap(FSP FS_GRAYMAP *);
EXTERN FS_GRAYMAP *pixelbold_graymap2(FSP FS_GRAYMAP *);
EXTERN FS_GRAYMAP *pixelbold_graymap4(FSP FS_GRAYMAP *);
EXTERN FS_GRAYMAP *pixelbold_graymap8(FSP FS_GRAYMAP *);
EXTERN UW16 uncompress_graymap(FSP PIFBITMAP);
EXTERN VOID bmap_to_gmap(FSP FS_GRAYMAP *, PIFBITMAP);
#endif	/* GRAYSCALING */
#endif	/* BOLD_CJK */

#ifdef FS_EDGE_TECH
EXTERN FS_VOID fixed_norm(FIXED_VECTOR *);
EXTERN ADF_FS_VOID startADF(FSP0);
EXTERN ADF_FS_VOID stopADF(FSP0);

EXTERN VOID get_scaling_matrix_params(FSP CGFIXED *fsin, CGFIXED *fcos, FPNUM *set, FPNUM *pnt, CGFIXED *fssin, CGFIXED *fscos);

#if FS_EDGE_RENDER
EXTERN ADF_FS_VOID adfSetDefaultRenderAttrs(ADFRenderAttrs  *adfRenderAttrs);

EXTERN MEM_HANDLE trim_graymap(_DS_ MEM_HANDLE);
#endif
#if FS_EDGE_HINTS
EXTERN ADF_FS_VOID adfGetEdgeHintData(FSP FS_ULONG glyph_index, FS_USHORT *EdgeHintType,
                           FS_SHORT *noCenter, FS_SHORT *cvtstart, 
                           FS_SHORT *numylines, FS_SHORT *isrighttoleft);
#endif
EXTERN MEM_HANDLE make_ADF_graymap(_DS_ FS_OUTLINE *, FS_ULONG, FS_USHORT,FS_USHORT);
#endif	/* defined(FS_EDGE_TECH) */

#if FS_EDGE_HINTS
EXTERN ADF_FS_VOID ADFMAZKeepInBox (fnt_LocalGraphicStateType *);
EXTERN ADF_I32 ADFAutohintMAZStroke (fnt_LocalGraphicStateType *);
EXTERN ADF_I32 ADFAutohintMAZOutline (fnt_LocalGraphicStateType *);
EXTERN ADF_FS_VOID BAZpositionStems(fnt_LocalGraphicStateType *);
EXTERN ADF_I32 ADFAutohintBAZStroke (fnt_LocalGraphicStateType *);
EXTERN ADF_I32 ADFAutohintBAZOutline (fnt_LocalGraphicStateType *);
#endif

/*------ start of very long not-LINT_ARGS set of declarations -----*/
#else

typedef struct {
    UW16 (*load_font) ();
    UW16 (*unload_font) ();
    UW16 (*set_trans) ();
    UW16 (*set_char) ();
    UW16 (*make_gaso_and_stats) ();
    UW16 (*render) ();
#if WIDTHS
   UW16 (*get_width) ();
#endif /* WIDTHS */
} IF_FUNC_TBL;
typedef IF_FUNC_TBL * PIF_FUNC_TBL;


#if IF_RDR
/*
pointer callable UFST functions
*/
EXTERN UW16 ifload_font ();
EXTERN UW16 ifunload_font ();
EXTERN UW16 ifset_trans ();
EXTERN UW16 ifset_char ();
EXTERN UW16 ifmake_gaso_and_stats ();
EXTERN UW16 ifrender ();
#if WIDTHS
EXTERN UW16 ifget_width ();
#endif

#if FONTBBOX
EXTERN UW16 ifget_FontBBox();
#endif
#if FNT_METRICS
EXTERN UW16 if_get_FontMetrics();
#endif

EXTERN VOID       first_loop ();
EXTERN VOID       next_loop ();
EXTERN VOID       pixel_align ();
EXTERN VOID       sd_pixel_align ();
EXTERN UW16   intel_char();
EXTERN SW16VECTOR  des2bm();
EXTERN SW16VECTOR  inv_des2bm();
EXTERN LPSB8    fgseg();

EXTERN UW16       LIBmake_cd();

#if FONTBBOX && IF_PCLEOI
EXTERN LPUB8 PCLEO_charptr();
#endif

#endif  /* IF_RDR */


#if PST1_RDR
/*
pointer callable UFST functions
*/
EXTERN UW16 psload_font ();
EXTERN UW16 psunload_font ();
EXTERN UW16 psset_trans ();
EXTERN UW16 psset_char ();
EXTERN UW16 psmake_gaso_and_stats ();
EXTERN UW16 psrender ();
#if WIDTHS
EXTERN UW16 psget_width ();
#endif

#if FNT_METRICS
EXTERN UW16 ps_get_FontMetrics();
#endif

#if CFF_RDR
EXTERN UW16   StdStrToIndex();
#endif

EXTERN PVOID  IndexToCgid();
EXTERN UW16   cm_cgidToIndex();
EXTERN UW16   psnameToIndex();

EXTERN VOID transfn8 ();
EXTERN VOID free_charstrings ();
EXTERN VOID free_subrs ();
EXTERN SL32 inithints();
EXTERN VOID sort_hints();
EXTERN BOOLEAN ps_is_same();
#endif /* PST1_RDR */


#if TT_RDR

#if (TT_ROM || TT_TYPE42I)
GLOBAL SL32  ttload_ROMSfnt();
#endif

#if (TT_ROM || TT_DISK || TT_ROM_ACT)
#if STIK
EXTERN FS_OUTLINE * extract_outline();
EXTERN UB8 get_nstk_bit();
EXTERN FS_OUTLINE * expand_stik();
EXTERN UW16 do_auto_hint();
#endif	/* STIK */

#if (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS
EXTERN UW16 ttTBLinit ();
EXTERN VOID ttTBLexit ();
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_slot ();
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_font ();
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_table ();
EXTERN UL32 tt_GetTagOffset();
#endif	/* (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS */

EXTERN BOOLEAN ttfont_supported();
EXTERN BOOLEAN TableOffsetsAreValid();
EXTERN BOOLEAN RequiredTablesExist();
#endif	/* (TT_ROM || TT_DISK || TT_ROM_ACT) */

/*
pointer callable UFST functions
*/
EXTERN UW16 ttload_font ();
EXTERN UW16 ttunload_font ();
EXTERN UW16 ttset_trans ();
EXTERN UW16 ttset_char ();
EXTERN UW16 ttmake_gaso_and_stats ();
EXTERN UW16 ttrender ();
#if WIDTHS
EXTERN UW16 ttget_width ();
#endif

#if FONTBBOX
EXTERN UW16 ttget_FontBBox();
#endif
#if FNT_METRICS
EXTERN UW16 tt_get_FontMetrics();
#endif

EXTERN VOID*  tt_GetFragment();
EXTERN VOID	 set_plat_spec_lang();
EXTERN UW16  ttkanset_char ();
EXTERN UL32 tt_GetTagSize();
EXTERN UW16 tt_GetTagBuffer();
EXTERN SW16 verifytruetype();

#if FS_EDGE_DUMP_OUTLINE
VOID dump_EDGE_outline();
VOID dump_final_EDGE_outline();
#endif

#endif /* TT_RDR */


#if FCO_RDR
/*
pointer callable UFST functions
*/
EXTERN UW16 fco_load_font();
EXTERN UW16 fco_unload_font();
EXTERN UW16 fco_set_trans();
EXTERN UW16 fco_set_char();
EXTERN UW16 fco_make_gaso_and_stats();
EXTERN UW16 fco_render();
#if WIDTHS
EXTERN UW16 fco_get_width();
#endif

#if FONTBBOX
EXTERN UW16 fco_get_FontBBox();
#endif
#if FNT_METRICS
EXTERN UW16 fco_get_FontMetrics();
#endif
#if FCO2
EXTERN UW16 decode_stream ();
#endif

EXTERN VOID fco_searchPathCur();
#endif /* FCO_RDR */

#if (IF_RDR || PST1_RDR)
EXTERN UW16  fco_map_cgnum_to_unicode();
#endif

#if CACHE || CHAR_HANDLE 
EXTERN UW16    makechar();
#endif

#if CHAR_SIZE
EXTERN UW16    MAKfontSize();
#endif

#if NON_Z_WIND
EXTERN VOID nz_init();
EXTERN VOID nz_open_run();
EXTERN VOID nz_close_run();
EXTERN UW16 nz_set_trans();
EXTERN UW16 checkContours();
#endif

#if OUTLINE
EXTERN VOID    out_setRender();
#if OLDATA
EXTERN UW16      outbuffer();
#endif
#endif

#if TILE
EXTERN UW16    bitmap_tile();
#endif

#if DISK_FONTS
EXTERN UW16      IXopen_file();
EXTERN VOID      IXclose_file();
EXTERN LPSB8     FIpath();
#endif

#if LINKED_FONT
EXTERN UW16 LFNT_SetFONTCONTEXT();
EXTERN UW16 LFNT_MAKifbmheader();
#endif

#if UFST_MULTITHREAD
EXTERN UW16 remove_thread_id_from_list();
EXTERN UW16 add_thread_id_to_list();
EXTERN VOID print_bucket_status();
EXTERN BOOLEAN thread_list_is_empty();
EXTERN BOOLEAN only_thread_in_list();
EXTERN VOID print_current_thread();
#endif	/* UFST_MULTITHREAD */

#if FIX_CONTOURS
EXTERN UW16  fix_contours();
#endif

#if XPS_SIDEWAYS
EXTERN UW16 calc_XPS_attributes();
#endif

#if BOLD_P6 || BOLD_HORIZONTAL || BOLD_VERTICAL 
EXTERN VOID smear_hookRender ();
EXTERN UW16	smear_calcvec();
#endif

#if BOLD_HORIZONTAL
EXTERN UW16   smear_setboldhorizontal ();
#endif

#if BOLD_VERTICAL
EXTERN UW16   smear_setboldvertical ();
#endif

#if BOLD_P6
EXTERN UW16   smear_setboldp6 ();
#endif

#if BOLD_CJK
EXTERN FS_BITMAP *pixelbold_bitmap();
EXTERN VOID bmap_to_bmap();

#if GRAYSCALING
EXTERN UL32 uncompressed_graymap_size();
EXTERN FS_GRAYMAP *pixelbold_graymap();
EXTERN FS_GRAYMAP *pixelbold_graymap2();
EXTERN FS_GRAYMAP *pixelbold_graymap4();
EXTERN FS_GRAYMAP *pixelbold_graymap8();
EXTERN UW16 uncompress_graymap();
EXTERN VOID bmap_to_gmap();
#endif	/* GRAYSCALING */
#endif	/* BOLD_CJK */

#ifdef FS_EDGE_TECH
EXTERN FS_VOID fixed_norm();
EXTERN ADF_FS_VOID startADF();
EXTERN ADF_FS_VOID stopADF();

EXTERN VOID get_scaling_matrix_params();

#if FS_EDGE_RENDER
EXTERN ADF_FS_VOID adfSetDefaultRenderAttrs(s);

EXTERN MEM_HANDLE trim_graymap();
#endif
#if FS_EDGE_HINTS
EXTERN ADF_FS_VOID adfGetEdgeHintData();
#endif
EXTERN MEM_HANDLE make_ADF_graymap();
#endif	/* defined(FS_EDGE_TECH) */

#if FS_EDGE_HINTS
EXTERN ADF_FS_VOID ADFMAZKeepInBox ();
EXTERN ADF_I32 ADFAutohintMAZStroke ();
EXTERN ADF_I32 ADFAutohintMAZOutline ();
EXTERN ADF_FS_VOID BAZpositionStems();
EXTERN ADF_I32 ADFAutohintBAZStroke ();
EXTERN ADF_I32 ADFAutohintBAZOutline ();
#endif

#endif /* LINT_ARGS */
/*------ END very long LINT_ARGS set of declarations -----*/


#if UFST_REENTRANT
#ifdef LINT_ARGS
UFST_EXTERNAL_INTERFACE MEM_HANDLE CGIFnew_client(FSP0);
UFST_EXTERNAL_INTERFACE SL32 CGIFend_client(FSP MEM_HANDLE);
#else
UFST_EXTERNAL_INTERFACE MEM_HANDLE CGIFnew_client();
UFST_EXTERNAL_INTERFACE SL32 CGIFend_client();
#endif	/* LINT_ARGS */
#endif	/* UFST_REENTRANT */


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifdef LINT_ARGS
UFST_EXTERNAL_INTERFACE UW16 CGIFconfig(FSP PIFCONFIG);
UFST_EXTERNAL_INTERFACE SW16 CGIFinitRomInfo(FSP LPUB8, LPUB8*);
UFST_EXTERNAL_INTERFACE  UW16   CGIFfund  (FSP UW16, LPSB8, UL32, LPUW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFmove_block (FSP UW16, LPSB8);

#if TT_TTPLUG && !FCO_RDR
UFST_EXTERNAL_INTERFACE  UW16   CGIFtt_universal(FSP LPUB8, SL32);
#endif
#if FONTBBOX
UFST_EXTERNAL_INTERFACE  UW16   CGIFbbox_IFPCLEOchar(FSP LPUB8, LPUB8, LPUW16);
#endif

#if NO_SYMSET_MAPPING
UFST_EXTERNAL_INTERFACE UW16   CGIFchIdptr( FSP VOID *, VOID * );
#endif
UFST_EXTERNAL_INTERFACE VOID   CGIFfont_access( FSP BOOLEAN );
UFST_EXTERNAL_INTERFACE VOID   CGIFwhat_version(UFST_VERSION_INFO *);

UFST_EXTERNAL_INTERFACE  UW16   CGIFinit  (FSP0);
UFST_EXTERNAL_INTERFACE  UW16   CGIFenter (FSP0);
UFST_EXTERNAL_INTERFACE  UW16   CGIFexit  (FSP0);
UFST_EXTERNAL_INTERFACE  UW16   CGIFfont  (FSP PFONTCONTEXT);

#if WIDTH_NOSYMMAP
UFST_EXTERNAL_INTERFACE  UW16   CGIFwidth (FSP PWIDTH_LIST_INPUT_ENTRY, UW16, UW16, LPUW16);
#else
UFST_EXTERNAL_INTERFACE  UW16   CGIFwidth (FSP UL32, UW16, UW16, LPUW16);
#endif

#if PST1_RDR
UFST_EXTERNAL_INTERFACE  UW16   CGIFget_design_metrics (FSP UL32, PGlyphMetrics_DU_GET);
UFST_EXTERNAL_INTERFACE  UW16   CGIFfree_charstrings(FSP0);
UFST_EXTERNAL_INTERFACE  UW16   CGIFfree_subrs(FSP0);
#endif

UFST_EXTERNAL_INTERFACE  UW16   CGIFchar  (FSP UL32, PPIFBITMAP, SW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFchar_by_ref  (FSP UL32, PPIFBITMAP, SW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFfree_by_ref  (FSP PIFBITMAP);
UFST_EXTERNAL_INTERFACE  UW16   CGIFfont_purge(FSP PFONTCONTEXT);
UFST_EXTERNAL_INTERFACE  UL32   CGIFbucket_purge(FSP UL32);
UFST_EXTERNAL_INTERFACE  UL32   CGIFmem_purge(FSP UL32);
UFST_EXTERNAL_INTERFACE  UW16   CGIFhdr_font_purge(FSP PFONTCONTEXT);
UFST_EXTERNAL_INTERFACE  UW16   CGIFtile(FSP PIFTILE, PPIFBITMAP);
UFST_EXTERNAL_INTERFACE  UW16   CGIFchar_handle(FSP UL32, PHIFBITMAP, SW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFtile_handle(FSP PIFTILE, PHIFBITMAP);

#if (MAX_BM_BITS > 16)
UFST_EXTERNAL_INTERFACE  UW16   CGIFchar_size(FSP UL32, LPSL32, SW16);
#else
UFST_EXTERNAL_INTERFACE  UW16   CGIFchar_size(FSP UL32, LPUW16, SW16);
#endif

UFST_EXTERNAL_INTERFACE  UW16   CGIFmakechar(FSP PIFBITMAP, LPUB8);
UFST_EXTERNAL_INTERFACE  UW16   CGIFbmheader(FSP UL32, PIFBITMAP, SW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFtilebitMap(FSP PIFTILE, PIFBITMAP, LPUB8);
UFST_EXTERNAL_INTERFACE  UW16   CGIFdefund(FSP UW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFsegments(FSP SL32, UW16, LPUW16, LPSB8);
UFST_EXTERNAL_INTERFACE  UW16   CGIFtile_nzw_buf(FSP PIFTILE piftile, LPSL32 psize);

#if FONTBBOX
#if VLCOUTPUT
UFST_EXTERNAL_INTERFACE  UW16   CGIFbound_box( FSP SL32*, LPSW16 );
#else
UFST_EXTERNAL_INTERFACE  UW16   CGIFbound_box( FSP SL32* );
#endif
#endif

#if FCO_RDR
UFST_EXTERNAL_INTERFACE  UW16   CGIFfco_Open( FSP LPUB8, LPSW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFfco_Close( FSP SW16 );
UFST_EXTERNAL_INTERFACE  UW16   CGIFfco_Plugin( FSP SW16);
#endif

#if FCOACCESS
UFST_EXTERNAL_INTERFACE  UW16   CGIFfco_Access( FSP LPUB8, UW16, UW16, LPUW16, LPSB8 );
#endif

#if FNT_METRICS
UFST_EXTERNAL_INTERFACE  UW16   CGIFfont_metrics( FSP FONT_METRICS* );
#endif

#if (TT_ROM || TT_DISK || TT_ROM_ACT)
UFST_EXTERNAL_INTERFACE UW16  CGIFtt_cmap_query (FSP UB8 *pFont, UW16 uTTCIndex, CMAP_QUERY *ret);
UFST_EXTERNAL_INTERFACE UW16  CGIFtt_name_query (FSP UB8 *pFont, UW16 uTTCIndex, NAME_QUERY *ret);

#if (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS
UFST_EXTERNAL_INTERFACE UW16   CGIFtt_query_direct (FSP LPUB8, UL32, UW16, LPUL32, LPLPUB8);
UFST_EXTERNAL_INTERFACE UW16   CGIFtt_query_direct_free (FSP LPUB8 );
#endif
#endif	/* (TT_ROM || TT_DISK || TT_ROM_ACT) */

#if TT_RDR
UFST_EXTERNAL_INTERFACE UW16 CGIFchar_get_gpos_pts(FSP UW16, UW16, UW16 *, SL32 *, SL32 *);
UFST_EXTERNAL_INTERFACE UW16 CGIFchar_map  (FSP UL32, UW16 *);
UFST_EXTERNAL_INTERFACE SW16 CGIFget_kern_value(FSP UB8 *tableBuffer, UL32 id1, UL32 id2);
UFST_EXTERNAL_INTERFACE UW16  CGIFtt_query( FSP LPUB8, UL32, UW16, LPUL32, LPUB8);
UFST_EXTERNAL_INTERFACE UW16  CGIFttpcleo_validate(FSP LPUB8 HeaderDataPtr);
UFST_EXTERNAL_INTERFACE VOID CGIFset_variation_selector(FSP UL32 variation_selector);
#endif

#if TT_DISK
UFST_EXTERNAL_INTERFACE UW16  CGIFtt_validate_font (FSP UB8 *, TT_VALIDATION_ERRS *);
#endif

#if UFST_MULTITHREAD
UFST_EXTERNAL_INTERFACE SL32 CGIFobtain_app_mutex(FSP0);
UFST_EXTERNAL_INTERFACE SL32 CGIFrelease_app_mutex(FSP0);
#endif

UFST_EXTERNAL_INTERFACE VOID CGIFCHARfree(FSP MEM_HANDLE);

#ifdef FS_EDGE_TECH
UFST_EXTERNAL_INTERFACE FS_LONG UFST_set_adfFlags(_DS_ FS_ULONG flag);
#endif

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#else	/* !LINT_ARGS */

UFST_EXTERNAL_INTERFACE  UW16   CGIFconfig();
UFST_EXTERNAL_INTERFACE  SW16   CGIFinitRomInfo();
UFST_EXTERNAL_INTERFACE  UW16   CGIFfund();
UFST_EXTERNAL_INTERFACE  UW16   CGIFmove_block();

#if TT_TTPLUG && !FCO_RDR
UFST_EXTERNAL_INTERFACE  UW16   CGIFtt_universal();
#endif

#if FONTBBOX
UFST_EXTERNAL_INTERFACE  UW16   CGIFbbox_IFPCLEOchar();
#endif

#if NO_SYMSET_MAPPING
UFST_EXTERNAL_INTERFACE  UW16   CGIFchIdptr();
#endif
UFST_EXTERNAL_INTERFACE  VOID   CGIFfont_access();
UFST_EXTERNAL_INTERFACE VOID CGIFwhat_version();
UFST_EXTERNAL_INTERFACE  UW16   CGIFinit();
UFST_EXTERNAL_INTERFACE  UW16   CGIFenter();
UFST_EXTERNAL_INTERFACE  UW16   CGIFexit();
UFST_EXTERNAL_INTERFACE  UW16   CGIFfont();
UFST_EXTERNAL_INTERFACE  UW16   CGIFwidth();

#if PST1_RDR
UFST_EXTERNAL_INTERFACE  UW16   CGIFget_design_metrics ();
UFST_EXTERNAL_INTERFACE  UW16   CGIFfree_charstrings();
UFST_EXTERNAL_INTERFACE  UW16   CGIFfree_subrs();
#endif

UFST_EXTERNAL_INTERFACE  UW16   CGIFchar();
UFST_EXTERNAL_INTERFACE  UW16   CGIFchar_by_ref();
UFST_EXTERNAL_INTERFACE  UW16   CGIFfree_by_ref();
UFST_EXTERNAL_INTERFACE  UW16   CGIFfont_purge();
UFST_EXTERNAL_INTERFACE  UL32   CGIFbucket_purge();
UFST_EXTERNAL_INTERFACE  UL32   CGIFmem_purge();
UFST_EXTERNAL_INTERFACE  UW16   CGIFhdr_font_purge();
UFST_EXTERNAL_INTERFACE  UW16   CGIFtile();
UFST_EXTERNAL_INTERFACE  UW16   CGIFchar_handle();
UFST_EXTERNAL_INTERFACE  UW16   CGIFtile_handle();
UFST_EXTERNAL_INTERFACE  UW16   CGIFchar_size();
UFST_EXTERNAL_INTERFACE  UW16   CGIFmakechar();
UFST_EXTERNAL_INTERFACE  UW16   CGIFbmheader();
UFST_EXTERNAL_INTERFACE  UW16   CGIFtilebitMap();
UFST_EXTERNAL_INTERFACE  UW16   CGIFdefund();
UFST_EXTERNAL_INTERFACE  UW16   CGIFsegments();
UFST_EXTERNAL_INTERFACE  UW16   CGIFtile_nzw_buf();

#if FONTBBOX
UFST_EXTERNAL_INTERFACE  UW16   CGIFbound_box();
#endif

#if FCO_RDR
UFST_EXTERNAL_INTERFACE  UW16   CGIFfco_Open();
UFST_EXTERNAL_INTERFACE  UW16   CGIFfco_Close();
UFST_EXTERNAL_INTERFACE  UW16   CGIFfco_Plugin();
#endif
	
#if FCOACCESS
UFST_EXTERNAL_INTERFACE  UW16   CGIFfco_Access();
#endif

#if FNT_METRICS
UFST_EXTERNAL_INTERFACE  UW16   CGIFfont_metrics();
#endif

#if (TT_ROM || TT_DISK || TT_ROM_ACT)
UFST_EXTERNAL_INTERFACE UW16  CGIFtt_cmap_query ();
UFST_EXTERNAL_INTERFACE UW16  CGIFtt_name_query ();

#if (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS
UFST_EXTERNAL_INTERFACE UW16   CGIFtt_query_direct ();
UFST_EXTERNAL_INTERFACE UW16   CGIFtt_query_direct_free ();
#endif
#endif	/* (TT_ROM || TT_DISK || TT_ROM_ACT) */

#if TT_RDR
UFST_EXTERNAL_INTERFACE  UW16 CGIFchar_get_gpos_pts();
UFST_EXTERNAL_INTERFACE  UW16 CGIFchar_map();
UFST_EXTERNAL_INTERFACE SW16 CGIFget_kern_value();
UFST_EXTERNAL_INTERFACE UW16  CGIFtt_query();
UFST_EXTERNAL_INTERFACE UW16  CGIFttpcleo_validate();
UFST_EXTERNAL_INTERFACE VOID CGIFset_variation_selector();

#endif

#if TT_DISK
UFST_EXTERNAL_INTERFACE UW16  CGIFtt_validate_font();
#endif

#if UFST_MULTITHREAD
UFST_EXTERNAL_INTERFACE  SL32 CGIFobtain_app_mutex();
UFST_EXTERNAL_INTERFACE  SL32 CGIFrelease_app_mutex();
#endif

UFST_EXTERNAL_INTERFACE  VOID CGIFCHARfree();

#ifdef FS_EDGE_TECH
UFST_EXTERNAL_INTERFACE FS_LONG UFST_set_adfFlags(_DS_ FS_ULONG flag);
#endif

#endif	/* LINT_ARGS */
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/


/*----------------------------------------------------------------------*/
/* single threaded versions - one of two versions if MULTITHREAD is defined */
#ifdef LINT_ARGS
UFST_EXTERNAL_INTERFACE  UW16   CGIFFinit  (FSP0);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFenter (FSP0);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFexit  (FSP0);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfont  (FSP PFONTCONTEXT);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFchar  (FSP UL32, PPIFBITMAP, SW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFchar_by_ref  (FSP UL32, PPIFBITMAP, SW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfree_by_ref  (FSP PIFBITMAP);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfont_purge(FSP PFONTCONTEXT);
UFST_EXTERNAL_INTERFACE  UL32   CGIFFbucket_purge(FSP UL32);
UFST_EXTERNAL_INTERFACE  UL32   CGIFFmem_purge(FSP UL32);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFhdr_font_purge(FSP PFONTCONTEXT);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFtile(FSP PIFTILE, PPIFBITMAP);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFchar_handle(FSP UL32, PHIFBITMAP, SW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFtile_handle(FSP PIFTILE, PHIFBITMAP);
#if (MAX_BM_BITS > 16)
UFST_EXTERNAL_INTERFACE  UW16   CGIFFchar_size(FSP UL32, LPSL32, SW16); 
#else
UFST_EXTERNAL_INTERFACE  UW16   CGIFFchar_size(FSP UL32, LPUW16, SW16);
#endif
UFST_EXTERNAL_INTERFACE  UW16   CGIFFmakechar(FSP PIFBITMAP, LPUB8);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFbmheader(FSP UL32, PIFBITMAP, SW16);

UFST_EXTERNAL_INTERFACE  UW16   CGIFFtilebitMap(FSP PIFTILE, PIFBITMAP, LPUB8);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFdefund(FSP UW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFsegments(FSP SL32, UW16, LPUW16, LPSB8);
#if FONTBBOX
#if VLCOUTPUT
UFST_EXTERNAL_INTERFACE  UW16   CGIFFbound_box( FSP SL32*, LPSW16 );
#else
UFST_EXTERNAL_INTERFACE  UW16   CGIFFbound_box( FSP SL32* );
#endif
#endif
#if FCO_RDR
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfco_Open( FSP LPUB8, LPSW16);
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfco_Close( FSP SW16 );
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfco_Plugin( FSP SW16);
#endif
#if FCOACCESS
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfco_Access( FSP LPUB8, UW16, UW16, LPUW16, LPSB8 );
#endif
#if FNT_METRICS
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfont_metrics( FSP FONT_METRICS* );
#endif

#if PST1_RDR
UFST_EXTERNAL_INTERFACE  UW16  CGIFFget_design_metrics (FSP UL32, PGlyphMetrics_DU_GET);
UFST_EXTERNAL_INTERFACE  VOID  CGIFFfree_charstrings(FSP0);
UFST_EXTERNAL_INTERFACE  VOID  CGIFFfree_subrs(FSP0);
#endif

#if WIDTH_NOSYMMAP
UFST_EXTERNAL_INTERFACE  UW16   CGIFFwidth (FSP PWIDTH_LIST_INPUT_ENTRY, UW16, UW16, LPUW16);
#else
UFST_EXTERNAL_INTERFACE  UW16   CGIFFwidth (FSP UL32, UW16, UW16, LPUW16);
#endif

#if CHAR_SIZE && TILE && NON_Z_WIND
UFST_EXTERNAL_INTERFACE  UW16   CGIFFtile_nzw_buf(FSP PIFTILE piftile, LPSL32 psize);
#endif

#if (TT_ROM || TT_DISK || TT_ROM_ACT)
UFST_EXTERNAL_INTERFACE UW16  CGIFFtt_cmap_query (FSP UB8 *pFont, UW16 uTTCIndex, CMAP_QUERY *ret);
UFST_EXTERNAL_INTERFACE UW16  CGIFFtt_name_query (FSP UB8 *pFont, UW16 uTTCIndex, NAME_QUERY *ret);

#if (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS
UFST_EXTERNAL_INTERFACE UW16   CGIFFtt_query_direct (FSP LPUB8, UL32, UW16, LPUL32, LPLPUB8);
UFST_EXTERNAL_INTERFACE UW16   CGIFFtt_query_direct_free (FSP LPUB8 );
#endif
#endif	/* (TT_ROM || TT_DISK || TT_ROM_ACT) */

#if TT_RDR
UFST_EXTERNAL_INTERFACE UW16 CGIFFchar_get_gpos_pts(FSP UW16, UW16, UW16 *, SL32 *, SL32 *);
UFST_EXTERNAL_INTERFACE UW16 CGIFFchar_map  (FSP UL32, UW16 *);
UFST_EXTERNAL_INTERFACE SW16 CGIFFget_kern_value(FSP UB8 *tableBuffer, UL32 id1, UL32 id2);
UFST_EXTERNAL_INTERFACE UW16  CGIFFtt_query( FSP LPUB8, UL32, UW16, LPUL32, LPUB8);
UFST_EXTERNAL_INTERFACE UW16  CGIFFttpcleo_validate(FSP LPUB8 HeaderDataPtr);

#endif

#if TT_DISK
UFST_EXTERNAL_INTERFACE UW16  CGIFFtt_validate_font (FSP UB8 *,  TT_VALIDATION_ERRS *);
#endif

UFST_EXTERNAL_INTERFACE VOID CHARRfree(FSP MEM_HANDLE);

/*----------------------------------------------------------------------*/
#else	/* !LINT_ARGS */
UFST_EXTERNAL_INTERFACE  UW16   CGIFFinit();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFenter();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFexit();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfont();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFchar();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFchar_by_ref();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfree_by_ref();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfont_purge();
UFST_EXTERNAL_INTERFACE  UL32   CGIFFbucket_purge();
UFST_EXTERNAL_INTERFACE  UL32   CGIFFmem_purge();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFhdr_font_purge();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFtile();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFchar_handle();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFtile_handle();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFchar_size();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFmakechar();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFbmheader();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFtilebitMap();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFdefund();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFsegments();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFbound_box();
#if FCO_RDR
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfco_Open();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfco_Close();
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfco_Plugin();
#endif
#if FCOACCESS
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfco_Access();
#endif /* FCOACCESS */
#if FNT_METRICS
UFST_EXTERNAL_INTERFACE  UW16   CGIFFfont_metrics();
#endif  /* FNT_METRICS */

#if PST1_RDR
UFST_EXTERNAL_INTERFACE  UW16  CGIFFget_design_metrics();
UFST_EXTERNAL_INTERFACE  VOID  CGIFFfree_charstrings();
UFST_EXTERNAL_INTERFACE  VOID  CGIFFfree_subrs();
#endif

UFST_EXTERNAL_INTERFACE  UW16   CGIFFwidth();
#if CHAR_SIZE && TILE && NON_Z_WIND
UFST_EXTERNAL_INTERFACE  UW16   CGIFFtile_nzw_buf();
#endif

#if (TT_ROM || TT_DISK || TT_ROM_ACT)
UFST_EXTERNAL_INTERFACE UW16  CGIFFtt_cmap_query ();
UFST_EXTERNAL_INTERFACE UW16  CGIFFtt_name_query ();

#if (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS
UFST_EXTERNAL_INTERFACE UW16   CGIFFtt_query_direct ();
UFST_EXTERNAL_INTERFACE UW16   CGIFFtt_query_direct_free ();
#endif
#endif	/* (TT_ROM || TT_DISK || TT_ROM_ACT) */

#if TT_RDR
UFST_EXTERNAL_INTERFACE UW16 CGIFFchar_get_gpos_pts();
UFST_EXTERNAL_INTERFACE UW16 CGIFFchar_map();
UFST_EXTERNAL_INTERFACE SW16 CGIFFget_kern_value();
UFST_EXTERNAL_INTERFACE UW16  CGIFFtt_query();
UFST_EXTERNAL_INTERFACE UW16  CGIFFttpcleo_validate();
#endif

#if TT_DISK
UFST_EXTERNAL_INTERFACE UW16  CGIFtt_validate_font (FSP UB8 *, TT_VALIDATION_ERRS *);
#endif

UFST_EXTERNAL_INTERFACE VOID CHARRfree();

#endif /* LINT_ARGS */
/*----------------------------------------------------------------------*/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#if !UFST_MULTITHREAD
/* change the names */
#define CGIFinit CGIFFinit
#define CGIFenter CGIFFenter
#define CGIFexit CGIFFexit
#define CGIFfont CGIFFfont
#define CGIFchar CGIFFchar
#define CGIFchar_by_ref CGIFFchar_by_ref
#define CGIFfree_by_ref CGIFFfree_by_ref
#define CGIFfont_purge CGIFFfont_purge
#define CGIFbucket_purge CGIFFbucket_purge
#define CGIFmem_purge CGIFFmem_purge
#define CGIFhdr_font_purge CGIFFhdr_font_purge
#define CGIFtile CGIFFtile
#define CGIFchar_handle CGIFFchar_handle
#define CGIFtile_handle CGIFFtile_handle
#define CGIFchar_size CGIFFchar_size
#define CGIFmakechar CGIFFmakechar
#define CGIFbmheader CGIFFbmheader
#define CGIFtilebitMap CGIFFtilebitMap
#define CGIFdefund CGIFFdefund
#define CGIFsegments CGIFFsegments
#define CGIFbound_box CGIFFbound_box
#define CGIFfco_Open CGIFFfco_Open
#define CGIFfco_Close CGIFFfco_Close
#define CGIFfco_Plugin CGIFFfco_Plugin
#define CGIFfco_Access CGIFFfco_Access
#define CGIFfont_metrics CGIFFfont_metrics
#define CGIFwidth CGIFFwidth
#define CGIFget_design_metrics CGIFFget_design_metrics
#define CGIFfree_charstrings CGIFFfree_charstrings
#define CGIFfree_subrs CGIFFfree_subrs

#define CGIFtile_nzw_buf CGIFFtile_nzw_buf

#define CGIFchar_get_gpos_pts CGIFFchar_get_gpos_pts
#define CGIFchar_map CGIFFchar_map

#define CGIFget_kern_value CGIFFget_kern_value 

#define CGIFtt_cmap_query CGIFFtt_cmap_query
#define CGIFtt_name_query CGIFFtt_name_query
#define CGIFtt_query_direct CGIFFtt_query_direct
#define CGIFtt_query_direct_free CGIFFtt_query_direct_free
#define CGIFtt_query CGIFFtt_query

#define CGIFinitUnicodeToJisTbl CGIFFinitUnicodeToJisTbl
#define CGIFinitUnicodeToKscTbl	CGIFFinitUnicodeToKscTbl
#define CGIFinitUnicodeToBig5Tbl CGIFFinitUnicodeToBig5Tbl
#define CGIFinitUnicodeToGbTbl CGIFFinitUnicodeToGbTbl
#define CGIFinitUnicodeToJohabTbl CGIFFinitUnicodeToJohabTbl 

#define CGIFinitJisToUnicodeTbl CGIFFinitJisToUnicodeTbl
#define CGIFinitKscToUnicodeTbl CGIFFinitKscToUnicodeTbl
#define CGIFinitBig5ToUnicodeTbl CGIFFinitBig5ToUnicodeTbl
#define CGIFinitGbToUnicodeTbl CGIFFinitGbToUnicodeTbl
#define CGIFinitJohabToUnicodeTbl CGIFFinitJohabToUnicodeTbl 

#define CGIFtt_validate_font CGIFFtt_validate_font
#define CGIFttpcleo_validate CGIFFttpcleo_validate

#define CGIFCHARfree CHARfree

#endif	/* !UFST_MULTITHREAD */
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/


/************** MEMORY MANAGEMENT **********************/
/* first case used only to build "fcodump2" at this time */
#if FCO_STANDALONE
#define  BUFalloc(size)  malloc((UL32)(size))
#define  BUFfree(p)      if (p) free(p)

#else
#if defined (ANSI_DEFS)
EXTERN MEM_HANDLE BUFalloc(FSP SL32);
EXTERN VOID       BUFfree(FSP MEM_HANDLE);
#else
EXTERN MEM_HANDLE BUFalloc();
EXTERN VOID       BUFfree();
#endif
#endif	/* FCO_STANDALONE */


#ifdef LINT_ARGS
EXTERN MEM_HANDLE  CHARalloc(FSP SL32);
EXTERN MEM_HANDLE  TEMPCHARalloc(FSP SL32);
EXTERN VOID        TEMPCHARfree(FSP MEM_HANDLE);
UFST_EXTERNAL_INTERFACE VOID CHARfree(FSP MEM_HANDLE);

#else   /*  not LINT_ARGS  */
EXTERN MEM_HANDLE  CHARalloc();
EXTERN MEM_HANDLE  TEMPCHARalloc();
EXTERN VOID        TEMPCHARfree();
UFST_EXTERNAL_INTERFACE VOID CHARfree();
#endif  /* LINTARGS */


#if INT_MEM_MGT   /*  INTERNAL memory manager  */

#ifdef LINT_ARGS
UFST_EXTERNAL_INTERFACE LPSB8     MEMptrFSP(FSP MEM_HANDLE);
#define MEMptr(h)    (LPSB8)MEMptrFSP(FSA h)

#else        /* !LINT_ARGS */
UFST_EXTERNAL_INTERFACE LPSB8      MEMptr();
#endif       /* !LINT_ARGS */

#else	/* EXTERNAL memory manager */

#define MEMptr(h)    (h)

#endif	/* INT_MEM_MGT */
/************** end MEMORY MANAGEMENT **********************/


/********** GRAYSCALING **********/
#if GRAYSCALING
/* Functions called from UFST core */
#ifdef LINT_ARGS
EXTERN UW16 grayfont(FSP PFONTCONTEXT);
EXTERN UW16 graymap(FSP PIFBITMAP, PHIFBITMAP);  /* Function called from gen\makechar.c */
EXTERN UW16 gs_endsubrast(FSP GRAYFILTER*);      /* Function called from transition processor */
EXTERN VOID grayexit(FSP0);
#if GRAYSTATS_1  /* debugging function, called from application */
EXTERN VOID gs_memstats(FSP SL32*, SL32*);
#endif
#else
EXTERN UW16 grayfont();
EXTERN UW16 graymap(); 
EXTERN UW16 gs_endsubrast();
EXTERN VOID grayexit();
#if GRAYSTATS_1  /* debugging function, called from application */
EXTERN VOID gs_memstats();
#endif
#endif

/* Interface functions for grayscale imaging */
#ifdef LINT_ARGS
UFST_EXTERNAL_INTERFACE UW16 gifont(GRAYIMAGE *gi, SW16  numXsubpixels, SW16  numYsubpixels,
                                  SW16 numXphases, SW16 numYphases);
UFST_EXTERNAL_INTERFACE UW16 gichar(FSP GRAYIMAGE*, PIFBITMAP, SL32, SL32);
EXTERN VOID BLACKPIX( FSP SW16 x, SW16 y );
EXTERN VOID GRAYPIX( FSP SW16 x, SW16 y, SW16 v );
EXTERN VOID UFST_BLACKPIX( FSP SW16 x, SW16 y, UW16 pixeldepth, UW16 black_width, LPUB8 bm );
EXTERN VOID UFST_GRAYPIX( FSP SW16 x, SW16 y, SW16 v, UW16 pixeldepth, UW16 black_width, LPUB8 bm );

#else
UFST_EXTERNAL_INTERFACE UW16 gifont();
UFST_EXTERNAL_INTERFACE UW16 gichar();
EXTERN VOID BLACKPIX();
EXTERN VOID GRAYPIX();
EXTERN VOID UFST_BLACKPIX();
EXTERN VOID UFST_GRAYPIX();
#endif  /* LINT_ARGS */
#endif	/* GRAYSCALING */
/********** end GRAYSCALING **********/


/********** NON-CONDITIONAL prototypes *********/
#ifdef LINT_ARGS
EXTERN BOOLEAN if_init_glob(FSP PBUCKET);
EXTERN UW16 IXinit(FSP0);
EXTERN VOID IXexit(FSP0);
EXTERN UW16 IXget_fnt_index(FSP PFONTCONTEXT);
EXTERN UW16 MTinit(FSP0);
EXTERN SL32 get_thread_id(FSP0);
EXTERN VOID init_former_globals(FSP0);

EXTERN SL32 CGIFcreate_mutex(FSP0);
EXTERN SL32 CGIFobtain_mutex(FSP0);
EXTERN SL32 CGIFrelease_mutex(FSP0);
EXTERN SL32 CGIFdelete_mutex(FSP0);
EXTERN SL32 CGIFcreate_app_mutex(FSP0);
EXTERN SL32 CGIFdelete_app_mutex(FSP0);

EXTERN UW16 DoEffects( FSP PIFBITMAP );
EXTERN SL32 HowMuchBiggerIsEffect(FSP UW16 num_loop, SW16 x_dim, SW16 y_dim, SW16 left_ind, SW16 black_w, BOOLEAN alwaysCompute);
EXTERN VOID    load_space_metrics(FSP PFONTCONTEXT, PIFBITMAP);
EXTERN UW16   comp_pix(FSP PFONTCONTEXT, PBUCKET);
EXTERN UW16   IXsetup_chr_def(FSP UL32, PCHR_DEF, PPBUCKET);
EXTERN UW16   DArd_char(FSP PCHR_DEF);
EXTERN UW16 DAmake_cd(FSP PBUCKET, UL32, PCHR_DEF, SL32);

GLOBAL VOID dd_open(PBUCKET pb);
GLOBAL SL32 dd_read(PBUCKET pb, VOID* buff, UL32 count);
GLOBAL SL32 dd_lseek(PBUCKET pb, SL32 pos, SL32 type);
GLOBAL VOID dd_close(PBUCKET pb);

EXTERN BOOLEAN     FMseek_read(FSP SL32, SL32, SL32, LPSB8);
EXTERN MEM_HANDLE  FMalloc_rd(FSP SL32, SL32, SL32,BOOLEAN);
EXTERN VOID        buildpath(LPSB8, LPSB8, LPSB8);
EXTERN PINDEX_ENTRY FIentry(FSP SL32);
EXTERN UW16        IXset_search_path(FSP SW16);

EXTERN UW16      IXmak_font_index(FSP LPSB8);
EXTERN UW16      BUCKfind(FSP SL32, SL32, PPBUCKET);
EXTERN UW16         BUCKinit(FSP0);
EXTERN VOID         BUCKexit(FSP0);
EXTERN VOID         BUCKfree(FSP HBUCKET);
EXTERN UW16 CACfont_free(FSP HIFFONT);

EXTERN VOID       MEMinit(FSP0);
EXTERN MEM_HANDLE MEMalloc(FSP UW16, SL32);
EXTERN VOID       MEMfree(FSP UW16, MEM_HANDLE);

EXTERN UW16    SYMmap(FSP SW16, SL32);
EXTERN UW16    SYMinit(FSP0);
EXTERN VOID    SYMexit(FSP0);
EXTERN BOOLEAN SYMnew(FSP UW16, UW16);
EXTERN BOOLEAN SYMnoMap(FSP LPUB8, UW16);

EXTERN UW16      cgoutline(FSP0);
EXTERN UW16      quadra(FSP UW16, INTR, INTR);
EXTERN UW16      cubic(FSP INTR, INTR);
EXTERN VOID  if_long_mult(SL32, SW16, LPSL32, LPSW16);

EXTERN UW16    map_and_set_scale(FSP PFONTCONTEXT, SL32); 
EXTERN SW16    frac_bits(SL32);
EXTERN UW16    MAKifbmheader(FSP PFONTCONTEXT, SL32, PIFBITMAP);
EXTERN UW16    bitmap(FSP PHIFBITMAP);
EXTERN UW16    outdata(FSP PHIFOUTLINE);
EXTERN UW16    app_make_character( PFONTCONTEXT, UW16, PHIFBITMAP );
EXTERN UW16    character(FSP0);
EXTERN UW16    chwidth(FSP UL32);

EXTERN UW16       make_gaso(FSP PBUCKET, SL32, PCHR_DEF);
EXTERN BOX        char_left_ref(FSP SW16VECTOR, BOX, SW16);
EXTERN SW16VECTOR find_part2whole(FSP SW16VECTOR, SW16VECTOR, PCHR_DEF);
EXTERN VOID       union_bound_boxes(PBOX, PBOX, SW16, SW16);
EXTERN VOID       cgfill(FSP LPUB8, LPUB8, SL32, SL32, LPUB8);
EXTERN VOID       metrics(FSP PIFBITMAP);
EXTERN VOID       merge(FSP PIFBITMAP, PIFBITMAP, SW16VECTOR);
EXTERN BOOLEAN	bitmap_rotated(FONTCONTEXT *);
EXTERN BOOLEAN	bitmap_sheared(FSP0);

EXTERN SW16VECTOR nxy(FSP PTRAN, PTRAN, SW16VECTOR);
EXTERN SW16       scale_iii(SW16, SW16, SW16);
EXTERN SW16VECTOR cg_scale (FSP SW16VECTOR);
EXTERN SB8 calc_px_offset(FSP0);

EXTERN SL32 varmul(SL32, SL32, INTG);
EXTERN SL32 varmul_64(FS_FIXED, FS_FIXED, INTG);
EXTERN FS_FIXED varmul_asm(FS_FIXED, FS_FIXED,INTG);
EXTERN FS_FIXED vardiv(FS_FIXED, FS_FIXED, INTG);
EXTERN SL32 vardiv_64(SL32, SL32, INTG);
EXTERN SL32 vardiv_asm(SL32, SL32, INTG);
EXTERN SL32 muldiv(SL32, SL32, SL32);
EXTERN SL32 muldiv_64(SL32, SL32, SL32);
EXTERN SL32 muldiv_asm(SL32,SL32,SL32);
EXTERN VOID normalize_16_16(FS_FIXED_POINT *);

#else
EXTERN BOOLEAN if_init_glob();
EXTERN UW16 IXinit();
EXTERN VOID IXexit();
EXTERN UW16 IXget_fnt_index();
EXTERN UW16 MTinit();
EXTERN SL32 get_thread_id();
EXTERN VOID init_former_globals();

EXTERN  SL32 CGIFcreate_mutex();
EXTERN  SL32 CGIFobtain_mutex();
EXTERN  SL32 CGIFrelease_mutex();
EXTERN  SL32 CGIFdelete_mutex();
EXTERN  SL32 CGIFcreate_app_mutex();
EXTERN  SL32 CGIFdelete_app_mutex();

EXTERN UW16 DoEffects();
EXTERN SL32 HowMuchBiggerIsEffect();
EXTERN VOID    load_space_metrics();
EXTERN UW16   comp_pix();
EXTERN UW16   IXsetup_chr_def();
EXTERN UW16   DArd_char();
EXTERN UW16 DAmake_cd();

GLOBAL VOID dd_open();
GLOBAL SL32 dd_read();
GLOBAL SL32 dd_lseek();
GLOBAL VOID dd_close();

EXTERN BOOLEAN     FMseek_read();
EXTERN MEM_HANDLE  FMalloc_rd();
EXTERN VOID        buildpath();
EXTERN PINDEX_ENTRY FIentry();
EXTERN UW16        IXset_search_path();

EXTERN UW16      IXmak_font_index();
EXTERN UW16      BUCKfind();
EXTERN UW16         BUCKinit();
EXTERN VOID         BUCKexit();
EXTERN VOID         BUCKfree();
EXTERN UW16 CACfont_free();

EXTERN VOID       MEMinit();
EXTERN MEM_HANDLE MEMalloc();
EXTERN VOID       MEMfree();

EXTERN UW16    SYMmap();
EXTERN UW16    SYMinit();
EXTERN VOID    SYMexit();
EXTERN BOOLEAN SYMnew();
EXTERN BOOLEAN SYMnoMap();

EXTERN UW16      cgoutline();
EXTERN UW16      quadra();
EXTERN UW16      cubic();
EXTERN VOID  if_long_mult();

EXTERN UW16    map_and_set_scale();
EXTERN SW16    frac_bits();
EXTERN UW16    MAKifbmheader();
EXTERN UW16    bitmap();
EXTERN UW16    outdata();
EXTERN UW16    app_make_character();
EXTERN UW16    character();
EXTERN UW16    chwidth();

EXTERN UW16       make_gaso();
EXTERN BOX        char_left_ref();
EXTERN SW16VECTOR find_part2whole();
EXTERN VOID       union_bound_boxes();
EXTERN VOID       cgfill();
EXTERN VOID       metrics();
EXTERN VOID       merge();
EXTERN BOOLEAN	bitmap_rotated();
EXTERN BOOLEAN	bitmap_sheared();

EXTERN SW16VECTOR nxy();
EXTERN SW16       scale_iii();
EXTERN SW16VECTOR cg_scale ();
EXTERN SB8 calc_px_offset();

EXTERN  SL32 varmul();
EXTERN  SL32 varmul_64();
EXTERN  FS_FIXED varmul_asm();
EXTERN  FS_FIXED vardiv();
EXTERN  SL32 vardiv_64();
EXTERN  SL32 vardiv_asm();
EXTERN  SL32 muldiv();
EXTERN  SL32 muldiv_64();
EXTERN  SL32 muldiv_asm();
EXTERN VOID normalize_16_16();

#endif


/********** PCLEO_RDR **********/
#if PCLEO_RDR
	
/* core code for PCLEO processing */
#ifdef LINT_ARGS
EXTERN UW16      PCLload_font(FSP LPUB8, PBUCKET);
EXTERN UW16      PCLEOmake_cd(FSP UW16, PCHR_DEF, SL32);
EXTERN VOID		 getHPFH( PHPFH, LPUB8 );
EXTERN LPUB8	 PCLchId2ptr (FSP UW16);
EXTERN UW16 	 PCLswapHdr(FSP LPUB8, UW16);
EXTERN UW16		 PCLswapChar(FSP LPUB8);


#if TT_PCLEOI
EXTERN LPUB8 PCLglyphID2Ptr( FSP UW16 );
EXTERN SL32  ttload_PCLEO (FSP LPUB8, PBUCKET);
#endif

EXTERN UW16    PCLeo2IF_ssnum( FSP PFONTCONTEXT, UW16, LPUW16 );

#else	/* not LINT_ARGS */
EXTERN UW16      PCLload_font();
EXTERN UW16      PCLEOmake_cd();
EXTERN VOID		 getHPFH();
EXTERN LPUB8	 PCLchId2ptr ();
EXTERN UW16 	 PCLswapHdr();
EXTERN UW16		 PCLswapChar();

#if TT_PCLEOI
EXTERN LPUB8 PCLglyphID2Ptr();
EXTERN SL32  ttload_PCLEO ();
#endif

EXTERN UW16    PCLeo2IF_ssnum();
#endif	/* LINT_ARGS */

/* demo code for PCLEO processing */
#ifdef LINT_ARGS
EXTERN SW16   installPcleo( FSP SB8 * );
EXTERN LPUB8                lastPcleoFontHdr( FSP0 );
EXTERN LPUB8                indextoPcleoFontHdr( FSP SL32 );
EXTERN VOID init_pcleo_callback_data( FSP0 );

#else  /* !LINT_ARGS */
EXTERN SW16   installPcleo();
EXTERN LPUB8  lastPcleoFontHdr();
EXTERN LPUB8  indextoPcleoFontHdr();
EXTERN VOID init_pcleo_callback_data();
#endif  /* LINT_ARGS */
#endif	/* PCLEO_RDR */
/********** end PCLEO_RDR **********/

#if TT_TYPE42I
/* demo code for TYPE42 processing */
#ifdef LINT_ARGS
EXTERN LPUB8  TYPE42glyphID2Ptr( FSP /* UW16 */ UL32 );
EXTERN VOID   *TYPE42Data2Ptr( FSP SL32, SL32 );
EXTERN UL32   TYPE42encoding_to_glyphindex( FSP UL32 );
EXTERN SW16   installType42( FSP SB8 * );
EXTERN LPUB8  indextoType42FontHdr( FSP SL32 );
EXTERN VOID   init_type42_callback_data( FSP0 );
#else  /* !LINT_ARGS */
EXTERN LPUB8  TYPE42glyphID2Ptr();
EXTERN VOID   *TYPE42Data2Ptr();
EXTERN UL32   TYPE42encoding_to_glyphindex();
EXTERN SW16   installType42();
EXTERN LPUB8  indextoType42FontHdr();
EXTERN VOID   init_type42_callback_data();
#endif  /* LINT_ARGS */
#endif	/* TT_TYPE42I */

/********** CGBITMAP or GRAYSCALING **********/
#if (CGBITMAP || GRAYSCALING)

/*  Output function interface structure for bitmaps... */
#ifdef LINT_ARGS
EXTERN UW16   bmras_start_char(FSPvoid PVOID, PVOID);
EXTERN UW16   bmras_end_char(FSPvoid PVOID);
EXTERN UW16   bmras_start_loop(FSPvoid PVOID, UW16);
EXTERN UW16   bmras_end_loop(FSPvoid PVOID);
EXTERN UW16   bmras_moveto(FSPvoid PVOID, INTR, INTR);
EXTERN UW16   bmras_lineto(FSPvoid PVOID, INTR, INTR);
EXTERN UW16   bmras_lineto_nzw(FSPvoid PVOID, INTR, INTR);
EXTERN UW16   bmras_lineto_oron(FSPvoid PVOID, INTR, INTR);
EXTERN UW16   bmras_lineto_oron_nzw(FSPvoid PVOID, INTR, INTR);
EXTERN UW16   bmras_lineto_oron_nzw_smear(FSPvoid PVOID, INTR, INTR);
EXTERN UW16   bmras_quadto(FSPvoid PVOID, INTR, INTR, INTR, INTR);

EXTERN VOID    bmp_setRender(FSP0);
#if STIK
EXTERN void   draw_quad(FSP FS_FIXED, FS_FIXED,FS_FIXED, FS_FIXED,FS_FIXED, FS_FIXED);
EXTERN void   draw_line(FSP FS_FIXED,FS_FIXED,FS_FIXED,FS_FIXED);
EXTERN void   draw_quad_AA(FSP FS_FIXED, FS_FIXED,FS_FIXED, FS_FIXED,FS_FIXED, FS_FIXED);
EXTERN void   draw_line_AA(FSP FS_FIXED,FS_FIXED,FS_FIXED,FS_FIXED);
#endif
#if PST1_RDR
EXTERN UW16   bmras_cubeto (FSPvoid PVOID, INTR, INTR, INTR, INTR, INTR, INTR);
#endif
#if (EMBEDDED_BITMAPS && TT_RDR)
EXTERN UW16		embedded_bitmap(FSP PIFBITMAP, PHIFBITMAP);
EXTERN UW16		embedded_character(FSP PEMBEDDED_BITMAP_FONT, PEMBEDDED_BITMAP, PIFBITMAP);
EXTERN VOID		get_embedded_bitmap_metrics(FSP PEMBEDDED_BITMAP_FONT, PEMBEDDED_BITMAP, PFONTCONTEXT, PIFBITMAP);
EXTERN BOOLEAN	embedded_bitmap_exists(FSP PEMBEDDED_BITMAP_FONT, PEMBEDDED_BITMAP, /* SW16 */ UW16);
EXTERN VOID	free_all_embedded_bitmap_items(FSP PEMBEDDED_BITMAP_FONT, PEMBEDDED_BITMAP, BOOLEAN);
#endif	/* EMBEDDED_BITMAPS && TT_RDR */

#else
EXTERN UW16   bmras_start_char();
EXTERN UW16   bmras_end_char();
EXTERN UW16   bmras_start_loop();
EXTERN UW16   bmras_end_loop();
EXTERN UW16   bmras_moveto();
EXTERN UW16   bmras_lineto();
EXTERN UW16   bmras_lineto_nzw();
EXTERN UW16   bmras_lineto_oron();
EXTERN UW16   bmras_lineto_oron_nzw();
EXTERN UW16   bmras_lineto_oron_nzw_smear();
EXTERN UW16   bmras_quadto();

EXTERN VOID    bmp_setRender();
#if STIK
EXTERN void   draw_quad();
EXTERN void   draw_line();
EXTERN void   draw_quad_AA();
EXTERN void   draw_line_AA();
#endif
#if PST1_RDR
EXTERN UW16   bmras_cubeto ();
#endif
#if (EMBEDDED_BITMAPS && TT_RDR)
EXTERN UW16		embedded_bitmap();
EXTERN UW16		embedded_character();
EXTERN VOID		get_embedded_bitmap_metrics();
EXTERN BOOLEAN	embedded_bitmap_exists();
EXTERN VOID	free_all_embedded_bitmap_items();
#endif	/* EMBEDDED_BITMAPS && TT_RDR */

#endif	/* LINT_ARGS */
#endif	/* (CGBITMAP || GRAYSCALING) */
/********** end CGBITMAP or GRAYSCALING **********/


/********** FLOATING POINT ***********/
/*  Function definitions  */
#ifdef LINT_ARGS
EXTERN FPNUM   fplongexp(SL32, SW16); /* Convert SL32 to FPNUM  */
EXTERN SW16    fp2word (FPNUM);        /*         FPNUM to SW16  */
EXTERN SL32    fp2long (FPNUM);        /*         FPNUM to SL32  */
EXTERN FPNUM   fpdivFSP (FSP FPNUM, FPNUM);   /* Divide                 */
EXTERN SW16    fp2intel(FSP FPNUM, LPSW16);
#ifdef AGFADEBUG
EXTERN VOID    fpprint(FSP FPNUM);        /* print to stdout fpnum  */
#endif
#else
EXTERN FPNUM   fplongexp();
EXTERN SW16    fp2word ();        /*         FPNUM to SW16  */
EXTERN SL32    fp2long ();        /*         FPNUM to SL32  */
EXTERN FPNUM   fpdivFSP ();   /* Divide                 */
EXTERN SW16    fp2intel();
#ifdef AGFADEBUG
EXTERN VOID    fpprint();
#endif
#endif

#define fpdiv(n1, n2) fpdivFSP (FSA n1, n2)

/*  Define an FPNUM to be a double in port.h  */

/*  Macro definitions  */

#define fpint2fp(x)   ((FPNUM)(x))              /* Convert SL32 to FPNUM  */
#define fpfixed2fp(x) ((FPNUM)(x)/65536.0)      /*         FIXED to FPNUM */
#define fpadd(x, y)   ((x)+(y))                 /* Add                    */
#define fpsub(x, y)   ((x)-(y))                 /* Subtract               */
#define fpmul(x, y)   ((x)*(y))                 /* Multiply               */
#define fpabs(x)      (((x)<0.0) ? (-(x)):(x))  /* Absolute value         */
#define ifpmul(x, y)  fp2word((FPNUM)x*y)       /* Multiply SW16 * FPNUM  */

#define FIXED2DOUBLE(x)((DOUBLE)(x)/65536.0)

/********** end FLOATING POINT ***********/


/********** CACHE **********/
#if CACHE
#ifdef LINT_ARGS
EXTERN UW16       CACinit(FSP0);
EXTERN VOID        CACexit(FSP0);
EXTERN UW16       CACfont(FSP PFONTCONTEXT);
EXTERN HIFBITMAP  CACinsert(FSP HIFBITMAP, UL32);
EXTERN UW16       CACpurge(FSP PFONTCONTEXT);
#if DEFUND
EXTERN BOOLEAN     CACdefund(FSP UW16);
#endif

EXTERN MEM_HANDLE CACbmLookup(FSP PFONT, UL32);
EXTERN UW16  CAChdr_purge (FSP PFONTCONTEXT);

#else   /*  not LINT_ARGS  */
EXTERN UW16       CACinit();
EXTERN VOID        CACexit();
EXTERN UW16       CACfont();
EXTERN HIFBITMAP  CACinsert();
EXTERN UW16       CACpurge();
#if DEFUND
EXTERN BOOLEAN     CACdefund();
#endif

EXTERN MEM_HANDLE CACbmLookup();
EXTERN UW16  CAChdr_purge ();
#endif
#endif /* CACHE */
/********** end CACHE **********/


/********** FCO_RDR **********/
#if FCO_RDR

#if defined (LINT_ARGS)
EXTERN UW16  FCOinit( FSP0 );
EXTERN UW16  FCOexit( FSP0 );
EXTERN UW16  fco_InitFCO( FSP SL32*, LPUB8 );
EXTERN UW16  fco_DeleteFCO( FSP SL32 );
EXTERN UW16  fco_saveHndl( FSP SL32 );
EXTERN UW16  fco_GetMemHandle( FSP SL32, PMEM_HANDLE );
EXTERN VOID  fco_pluginSearchPath( FSP PBUCKET, LPUB8, SL32* );

DAFILE* DAopen   (FSP LPUB8 fcName, DAFILE *f);
DAFILE* DAclose  (FSP DAFILE* f);
DAFILE* DAsetPos (FSP DAFILE* f, SL32 offset);
SW16   DAgetSWord(FSP DAFILE *f);
SL32    DAgetLong(FSP DAFILE *f);
SL32    DAgetSL32(FSP DAFILE*);
#if FCO_DISK
UINTG   DAseek_rd(FSP DAFILE*, SL32, UINTG, LPUB8);
void FC_RelMemPtr (FSP MEM_HANDLE h);
#endif

UW16      FCnew      ( FSP FCTYPE*, UB8* );
UW16      FCclose    ( FSP FCTYPE* );
SL32       FCnumVersion(FCTYPE* fc);
SL32       FCnumFonts (FCTYPE* fc);
UB8* FCgetStringsPtr (FSP FCTYPE* fc);
UB8* FCgetPostStringsPtr (FSP FCTYPE* fc);
UB8* FCgetYClassPtr  (FSP FCTYPE* fc);
UW16      FCaccessNew      ( FSP FCACCESSTYPE*, UB8*, INTG );

UW16       FCfontNew  ( FSP FONTTYPE*, FCTYPE*, INTG, INTG );
UW16       FCfontClose( FSP FONTTYPE* );
UW16       FCcharNew  ( FSP CHARTYPE*, FONTTYPE*, UINTG, INTG );
UW16       FCcharClose( FSP CHARTYPE* );
UW16       FCcompCharPiece( FSP COMPPIECETYPE*, CHARTYPE*, INTG );
UW16       FCcompCharShort( FSP CHARTYPE*, COMPPIECETYPE*, SL32*, SL32*,
                            INTG*, INTG* );
UW16       FCpluginNew( FSP FCTYPE*, TREETYPE* );
#if FONTBBOX || FNT_METRICS
UW16       FCfontInfoIndex( FSP TTFONTINFOTYPE*, FCTYPE*, INTG );
#endif
SL32        GETBIT( UB8**, SL32* );
SL32        GETDIBBLE( UB8**, SL32* );
SL32        GETNIBBLE( UB8**, SL32* );
SL32        GETBYTE( UB8**, SL32* );

#else	/* not LINT_ARGS */
EXTERN UW16  FCOinit();
EXTERN UW16  FCOexit();
EXTERN UW16  fco_InitFCO();
EXTERN UW16  fco_DeleteFCO();
EXTERN UW16  fco_GetMemHandle();
EXTERN UW16  fco_saveHndl();
EXTERN VOID  fco_pluginSearchPath();

DAFILE* DAopen   ();
DAFILE* DAclose  ();
DAFILE* DAsetPos ();
SW16   DAgetSWord();
SL32    DAgetLong();
SL32    DAgetSL32();
#if FCO_DISK
UINTG   DAseek_rd();
void FC_RelMemPtr ();
#endif

UW16      FCnew      ();
UW16      FCclose    ();
SL32       FCnumVersion();
SL32       FCnumFonts ();
UB8* FCgetStringsPtr();
UB8* FCgetPostStringsPtr();
UB8* FCgetYClassPtr ();
UW16     FCaccessNew();

UW16       FCfontNew();
UW16       FCfontClose();
UW16       FCcharNew();
UW16       FCcharClose();
UW16       FCcompCharPiece();
UW16       FCcompCharShort();
UW16       FCpluginNew();
#if FONTBBOX || FNT_METRICS
UW16       FCfontInfoIndex();
#endif
SL32        GETBIT();
SL32        GETDIBBLE();
SL32        GETNIBBLE();
SL32        GETBYTE();
#endif	/* LINT_ARGS */

#if defined (LINT_ARGS)
#if FCO3  /* MicroType 3 */
UL32 intelliflator( FSP
                            SW16*,
                            INFLATE*, 
                            MODELTYPE*,
                            UB8*,
                            UB8,
                            SL32*,
                            SL32*,
                            SL32*,
							UB8*,
							UB8*,
                            PIXEL_DATA*, 
                            PIXEL_DATA*, 
                            SL32,
                            UB8*, 
                            UL32, 
                            UL32,
                            UB8*,
                            SL32, SL32  );
#endif  /* FCO3 */
#if FCO2  /* MicroType 2 */
UL32 intelliflator( FSP
                            SW16*,
                            INFLATE*, 
                            MODELTYPE*,
                            UB8*,
                            UB8,
                            SL32*,
                            SL32*,
                            SL32*,
                            PIXEL_DATA*, 
                            PIXEL_DATA*, 
                            SL32,
                            UB8*, 
                            UL32, 
                            UL32,
                            UB8*,
                            SL32, SL32  );
#endif  /* FCO2 */
#if FCO1    /* MicroType 1 */
UL32 intelliflator( FSP INFLATE*, MODELTYPE*,
                            UB8*, SL32*, SL32*, SL32*,
                            PIXEL_DATA*, PIXEL_DATA*, SL32,
                            UB8*, SL32, SL32,
                            UB8*,
                            SL32, SL32, SL32, SL32 );
#endif   /* FCO1 */
void         in_VectorScaleGrid( SL32* xtr, SL32* ytr, PIXEL_DATA* x, PIXEL_DATA* y );

#if FCO1 || FCO2 || FCO3
SL32 scale (SL32, PIXEL_DATA *, BOOLEAN);
#endif	/* FCO1 || FCO2 || FCO3 */
#else /* not LINT_ARGS*/

UL32 intelliflator();
void         in_VectorScaleGrid();
SL32 scale ();
#endif
#endif	/* FCO_RDR */
/********** end FCO_RDR **********/


/********** ASIAN_ENCODING **********/
#if ASIAN_ENCODING
#ifdef LINT_ARGS
EXTERN UW16 SJisToJis (UW16);
EXTERN UW16 JisToSJis (UW16);
EXTERN UW16 EucToSJis (UW16);
EXTERN UW16 EucToJis  (UW16);
EXTERN UW16 JisToEuc  (UW16);
EXTERN UW16 SJisToEuc (UW16);

#if UFST_MULTITHREAD

#if UNICODE_IN
#if JIS_ENCODING
EXTERN UW16 CGIFinitUnicodeToJisTbl(FSP LPSB8, UL32);
#endif
#if K_ENCODING
EXTERN UW16 CGIFinitUnicodeToKscTbl(FSP LPSB8, UL32);
#endif
#if BIG5_ENCODING
EXTERN UW16 CGIFinitUnicodeToBig5Tbl(FSP LPSB8, UL32);
#endif
#if GB_ENCODING
EXTERN UW16 CGIFinitUnicodeToGbTbl(FSP LPSB8, UL32);
#endif
#if JOHAB_ENCODING
EXTERN UW16 CGIFinitUnicodeToJohabTbl(FSP LPSB8, UL32);
#endif
#endif /* UNICODE_IN */

#if (UNICODE_MAPPING & JIS2UNI_MAP)
EXTERN UW16 CGIFinitJisToUnicodeTbl(FSP LPSB8, UL32);
#endif
#if (UNICODE_MAPPING & KSC2UNI_MAP)
EXTERN UW16 CGIFinitKscToUnicodeTbl(FSP LPSB8, UL32);
#endif
#if (UNICODE_MAPPING & BIG52UNI_MAP)
EXTERN UW16 CGIFinitBig5ToUnicodeTbl(FSP LPSB8, UL32);
#endif
#if (UNICODE_MAPPING & GB2UNI_MAP)
EXTERN UW16 CGIFinitGbToUnicodeTbl(FSP LPSB8, UL32);
#endif
#if (UNICODE_MAPPING & JOHAB2UNI_MAP)
EXTERN UW16 CGIFinitJohabToUnicodeTbl(FSP LPSB8, UL32);
#endif

#endif	/* UFST_MULTITHREAD */

#if UNICODE_IN
#if JIS_ENCODING
EXTERN UW16 UnicodeToJis(FSP UW16);
EXTERN UW16 CGIFFinitUnicodeToJisTbl(FSP LPSB8, UL32);
#endif
#if K_ENCODING
EXTERN UW16 UnicodeToKsc(FSP UW16);
EXTERN UW16 CGIFFinitUnicodeToKscTbl(FSP LPSB8, UL32);
#endif
#if BIG5_ENCODING
EXTERN UW16 UnicodeToBig5(FSP UW16);
EXTERN UW16 CGIFFinitUnicodeToBig5Tbl(FSP LPSB8, UL32);
#endif
#if GB_ENCODING
EXTERN UW16 UnicodeToGb(FSP UW16);
EXTERN UW16 CGIFFinitUnicodeToGbTbl(FSP LPSB8, UL32);
#endif
#if JOHAB_ENCODING
EXTERN UW16 UnicodeToJohab(FSP UW16);
EXTERN UW16 CGIFFinitUnicodeToJohabTbl(FSP LPSB8, UL32);
#endif
#endif /* UNICODE_IN */

#if (UNICODE_MAPPING & JIS2UNI_MAP)
EXTERN UW16 JisToUnicode(FSP UW16);
EXTERN UW16 CGIFFinitJisToUnicodeTbl(FSP LPSB8, UL32);
#endif
#if (UNICODE_MAPPING & KSC2UNI_MAP)
EXTERN UW16 KscToUnicode(FSP UW16);
EXTERN UW16 CGIFFinitKscToUnicodeTbl(FSP LPSB8, UL32);
#endif
#if (UNICODE_MAPPING & BIG52UNI_MAP)
EXTERN UW16 Big5ToUnicode(FSP UW16);
EXTERN UW16 CGIFFinitBig5ToUnicodeTbl(FSP LPSB8, UL32);
#endif
#if (UNICODE_MAPPING & GB2UNI_MAP)
EXTERN UW16 GbToUnicode(FSP UW16);
EXTERN UW16 CGIFFinitGbToUnicodeTbl(FSP LPSB8, UL32);
#endif

#if (UNICODE_MAPPING & JOHAB2UNI_MAP)
EXTERN UW16 JohabToUnicode(FSP UW16);
EXTERN UW16 CGIFFinitJohabToUnicodeTbl(FSP LPSB8, UL32);
#endif
#if ((UNICODE_IN || (UNICODE_MAPPING & JIS2UNI_MAP) \
                 || (UNICODE_MAPPING & KSC2UNI_MAP) \
                 || (UNICODE_MAPPING & BIG52UNI_MAP)\
                 || (UNICODE_MAPPING & GB2UNI_MAP) \
                 || (UNICODE_MAPPING & JOHAB2UNI_MAP) ) && DISK_FONTS) 
EXTERN VOID ASIANexit(FSP0);
#endif

#else /* !LINT_ARGS */

EXTERN UW16 SJisToJis ();
EXTERN UW16 JisToSJis ();
EXTERN UW16 EucToSJis ();
EXTERN UW16 EucToJis  ();
EXTERN UW16 JisToEuc  ();
EXTERN UW16 SJisToEuc ();

#if UNICODE_IN
#if JIS_ENCODING
EXTERN UW16 UnicodeToJis();
EXTERN UW16 CGIFinitUnicodeToJisTbl();
#endif
#if K_ENCODING
EXTERN UW16 UnicodeToKsc();
EXTERN UW16 CGIFinitUnicodeToKscTbl();
#endif
#if BIG5_ENCODING
EXTERN UW16 UnicodeToBig5();
EXTERN UW16 CGIFinitUnicodeToBig5Tbl();
#endif
#if GB_ENCODING
EXTERN UW16 UnicodeToGb();
EXTERN UW16 CGIFinitUnicodeToGbTbl();
#endif
#if JOHAB_ENCODING
EXTERN UW16 UnicodeToJohab();
EXTERN UW16 CGIFinitUnicodeToJohabTbl();
#endif
#endif /* UNICODE_IN */

#if (UNICODE_MAPPING & JIS2UNI_MAP)
EXTERN UW16 JisToUnicode();
EXTERN UW16 CGIFinitJisToUnicodeTbl();
#endif
#if (UNICODE_MAPPING & KSC2UNI_MAP)
EXTERN UW16 KscToUnicode();
EXTERN UW16 CGIFinitKscToUnicodeTbl();
#endif
#if (UNICODE_MAPPING & BIG52UNI_MAP)
EXTERN UW16 Big5ToUnicode();
EXTERN UW16 CGIFinitBig5ToUnicodeTbl();
#endif
#if (UNICODE_MAPPING & GB2UNI_MAP)
EXTERN UW16 GbToUnicode();
EXTERN UW16 CGIFinitGbToUnicodeTbl();
#endif
#if (UNICODE_MAPPING & JOHAB2UNI_MAP)
EXTERN UW16 JohabToUnicode();
EXTERN UW16 CGIFinitJohabToUnicodeTbl();
#endif
#if ((UNICODE_IN || (UNICODE_MAPPING & JIS2UNI_MAP) \
                 || (UNICODE_MAPPING & KSC2UNI_MAP) \
                 || (UNICODE_MAPPING & BIG52UNI_MAP)\
                 || (UNICODE_MAPPING & GB2UNI_MAP) \
				 || (UNICODE_MAPPING & JOHAB2UNI_MAP) ) && DISK_FONTS) 
EXTERN VOID ASIANexit();
#endif
#endif /* LINT_ARGS */
#endif	/* ASIAN_ENCODING */
/********** end ASIAN_ENCODING **********/


/***** prototypes for various debugging routines *****/
#ifdef LINT_ARGS

/* access functions for if_state.trace_sw debug switch */
VOID UFST_debug_on(FSP0);
VOID UFST_debug_off(FSP0);
SW16 UFST_get_debug(FSP0);
VOID UFST_debug_file_open(FSP0);
VOID UFST_debug_file_close(FSP0);
#ifdef AGFADEBUG
EXTERN VOID       print_bm(FSP PIFBITMAP);
#endif

#if TT_ROM_ACT
EXTERN VOID print_code(UL32,SL32);
#endif	/* TT_ROM_ACT */

#if MEM_TRACE
GLOBAL VOID MEMstat (FSP UW16);
EXTERN SL32 CACdump (FSP UW16);
#endif /* MEM_TRACE */

#if PRINT_BUCKETS
print_buckets(FSP0);
#endif

#else	/* ! LINT_ARGS */

/* access functions for if_state.trace_sw debug switch */
VOID UFST_debug_on();
VOID UFST_debug_off();
SW16 UFST_get_debug();
VOID UFST_debug_file_open();
VOID UFST_debug_file_close();
#ifdef AGFADEBUG
EXTERN VOID       print_bm();
#endif

#if TT_ROM_ACT
EXTERN VOID print_code();
#endif	/* TT_ROM_ACT */

#if MEM_TRACE
GLOBAL VOID MEMstat ();
EXTERN SL32 CACdump ();
#endif /* MEM_TRACE */

#if PRINT_BUCKETS
print_buckets();
#endif
#endif	/* LINT_ARGS */
/***** END debugging prototypes *****/

#endif	/* __SHAREINC__ */
