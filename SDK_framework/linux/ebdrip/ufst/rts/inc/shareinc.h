/* $HopeName: GGEufst5!rts:inc:shareinc.h(EBDSDK_P.1) $ */

/* Copyright (C) 2004 Agfa Monotype Corporation. All rights reserved. */

/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:inc:shareinc.h,v 1.3.8.1.1.1 2013/12/19 11:24:04 rogerb Exp $ */

/***********************************************************************
 
	"shareinc.h":

	This is the sequence of include files that will be required for all files
	that reference "if_state", once the reentrancy work is complete. 
	
	Whereever possible, you should include the file "shareinc.h" rather than
	"if_type.h", in order to minimize build problems as global variables
	migrate into "if_state".

************************************************************************/

/*------------------------------------------------------------------------*/
/*  History
 *
 *  23-Mar-98  slg  Created for UFST reentrancy work.
 *	24-Mar-98  slg	Put EXTERN dcl for "if_state" here.
 *	25-Mar-98  slg	Also need "kanji.h" here.
 *  31-Mar-98  slg	Several MicroType files also needed.
 *	01-Apr-98  slg	PIXEL_DATA definition also needed.
 *  12-Jun-98  slg	Parameter-passing changes for reentrancy: several 
 *					conditional-on-UFST_REENTRANT #defines added, many
 *					function prototypes moved to end of this file (because
 *					the prototypes may now reference IF_STATE, so they need
 *					to come after the declaration of IF_STATE in if_type.h).
 *  18-Jun-98  slg	Also need "pcleo.h" here; more function prototypes moved
 *					here and/or modified for reentrancy (primarily TrueType
 *					and Intellifont functions)
 *	06-Jul-98  slg	Add conditional define of BUFalloc()/BUFfree(), based on
 *					FCO_STANDALONE value (used to build "fcodump2" utility)
 *	08-Jul-98  slg	Add Surfer data (conditional on USE_SURFER_API flag)
 *  15-Jul-98  tbh  Add function prototype for new intelliflator
 *  05-Aug-98  awr  changed !ROM to DISK_FONTS
 *  13-Aug-98  tbh  Added parameter to MM2 intelliflator
 *  24-Aug-98  jfd  Changed all references of MM2 to FCO2.
 *	02-Sep-98  slg	Move grayimaging prototypes from imagegr.h.
 *	04-Sep-98  slg	Placeholder: define UFST_EXTERNAL_INTERFACE unconditionally
 *					as EXTERN - later, there will be other options
 *	14-Sep-98  slg	Fix up fpmath prototypes - incomplete for non-LINT_ARGS case.
 *	05-Jan-99  slg	Add support for building of UFST as Windows DLL.
 *	12-Jan-99  slg	Move lots of EXTERN dcls here from .c files
 *	19-Jan-99  slg	Don't include cache.h, bitmap.h any more - contents moved
 *					into if_type.h (to resolve customer name conflict)
 *	27-Jan-99  sbm	Conditionally compile bmras_quadto based on TT_RDR and FCO_RDR.
 *  27-Jan-99  slg	Move EXTERN dcls for cm_cgidToIndex(), psnameToIndex(),
 *					PCLchId2ptr() here.
 *  08-Feb-99  dlk  Added EXTERN declarations for StdStrToIndex().
 *	13-Apr-99  ks	Moved MLOCAL declaration for smear_calcvec() to EXTERN declaration.
 *  21-Jun-99  jfd  Replaced functins get_ttDISK_ids(), get_ttROM_ids() and
 *                  get_ttROMACT_ids() with get_tt_ids().
 *  09-Jul-99  jfd  Conditionaly compiled prototype for get_tt_ids().
 *  28-July-99 ks	Changed DEBUG compiler directive to AGFADEBUG.
 *	17-Aug-99  slg	Add USING_16_BIT_DOS test; prototype and definition for 
 *					fco_map_cgnum_to_unicode() now use same test. 
 *	23-Aug-99  slg  Fix for (SIGNEDCHAR NO) - FCnew() parm = UB8*, not char*
 *	10-Nov-99  slg	Remove FCcopy() prototype (obsolete - inlined now).
 *	31-Jan-00  slg  Integrate disk/rom changes (for jd) - add CGIFfont_access()
 *					prototype; !ROM tests become DISK_FONTS.
 *  03-Feb-00  awr  Changed SWP799 to WINDCOMP
 *	08-Feb-00  slg	Lots of reentrancy fixes for simultaneous disk/ROM.
 * 28-Sep-00  jfd Added function prototypes for embedded bitmap functions.
 * 24-May-01  swp  Added prototypes for inithints() and sort_hints()
 *                 enabled by #define NEW_PS_HINTS
 * 09-Jul-01  slg  Group PCLEO prototypes together; tighten compile condition
 *					for CGIFtt_cmap_query()
 * 11-Jul-01  jfd  Changed type of last arg of embedded_bitmap_exists() prototype
 *                 from SW16 to UW16.
 * 31-Aug-01  slg  Make PCLchId2ptr() and PCLglyphID2Ptr() reentrant; move
 *					tt_GetFragment() prototype here.
 * 04-Sep-01  jfd  Added prototype for new function IndexToCgid().
 * 15-Aug-02  awr  Added DIMM_DISK
 * 12-Sep-02  awr  Declared checkContours() to fix badly wound contours for
 *                 CHAR_SIZE (in addition to all of the others)
 * 12-Jun-03  jfd  Added "cache-by-reference" support.
 * 13-Aug-03  jfd  Added "table-by-reference" support.
 */

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
/*-------------------------------------------------*/
					

#include "ifmem.h"		/* MEM_HANDLE */
#include "cgif.h"		/* DLL */
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
#include <setjmp.h>
#include "fscdefs.h"
#include "fontscal.h"
#include "fontmath.h"
#include "sfnt.h"
#include "fnt.h"
#include "fsglue.h"
#endif

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

/* moved from ifmem.h */
#if !INT_MEM_MGT	/* EXTERNAL memory manager */

#if (HUGE_PTR_SUPPORT)
#define hMEMptr(h)    (h)
#endif
#define MEMptr(h)    (h)

#endif

#ifdef LINT_ARGS
EXTERN SB8 calc_px_offset(FSP0);
#else
EXTERN SB8 calc_px_offset();
#endif

/*-------------------------------------------------*/
/* 
	EXTERNAL INTERFACES

All functions which can be called by an application should
be declared as UFST_EXTERNAL_INTERFACE, so that the
function can be conditionally compiled as a static-linked library,
a DLL library, or a DLL client.
-------------------------------------------------*/

#if (UFST_LINK_MODE ==  UFST_BUILD_DLL)
/* building UFST as a DLL */
#define UFST_EXTERNAL_INTERFACE _declspec (dllexport)

#elif (UFST_LINK_MODE ==  UFST_DLL_CLIENT)
/* building a UFST application to use the UFST DLL */
#define UFST_EXTERNAL_INTERFACE _declspec (dllimport)

#else	/* presumably UFST_LINK_MODE ==  UFST_STATIC_LINK */
/* either building UFST as a static library,  */
/* or building a UFST application to use UFST as a static library */
#define UFST_EXTERNAL_INTERFACE EXTERN
#endif
/*-------------------------------------------------*/

#ifdef LINT_ARGS
EXTERN VOID init_former_globals(FSP0);
#else
EXTERN VOID init_former_globals();
#endif

#if GRAYSCALING
/* Function(s) called from UFST core */
/* Moved from graymap.h */
#ifdef LINT_ARGS
EXTERN UW16 grayfont(FSP PFONTCONTEXT);
EXTERN UW16 graymap(FSP PIFBITMAP, PHIFBITMAP);  /* Function called from gen\makechar.c */
EXTERN UW16 gs_endsubrast(FSP GRAYFILTER*);      /* Function called from transition processor */
EXTERN VOID grayexit(FSP0);
#else
EXTERN UW16 grayfont();
EXTERN UW16 graymap(); 
EXTERN UW16 gs_endsubrast();
EXTERN VOID grayexit();
#endif

/* Interface functions for grayscale imaging */
/* Moved from imagegr.h */
#ifdef LINT_ARGS
UFST_EXTERNAL_INTERFACE UW16 gifont(GRAYIMAGE *gi, SW16  numXsubpixels, SW16  numYsubpixels,
                                  SW16 numXphases, SW16 numYphases);
#if 0	/* post-4.0 version */
UFST_EXTERNAL_INTERFACE UW16 gichar(FSP GRAYIMAGE*, PIFBITMAP, SL32, SL32,
	VOID (*BLACKPIX_fn) (FSP SW16, SW16, void *),
	VOID (*GRAYPIX_fn) (FSP SW16, SW16, SW16, void *),
	void * );
#else
UFST_EXTERNAL_INTERFACE UW16 gichar(FSP GRAYIMAGE*, PIFBITMAP, SL32, SL32);
#endif
EXTERN VOID BLACKPIX( FSP SW16 x, SW16 y );
EXTERN VOID GRAYPIX( FSP SW16 x, SW16 y, SW16 v );

#else
EXTERN UW16 gifont();
EXTERN UW16 gichar();
EXTERN VOID BLACKPIX();
EXTERN VOID GRAYPIX();
#endif  /* LINT_ARGS */
#endif	/* GRAYSCALING */


/* Moved from ix.h */
#ifdef LINT_ARGS
typedef struct {
    UW16 (*load_font) (FSP PFONT_DES, PBUCKET);
    UW16 (*unload_font) (FSP PBUCKET);
    UW16 (*set_trans) (FSP PBUCKET, SL32, SL32, SL32, SL32);
    UW16 (*set_char) (FSP PBUCKET, UL32);   /* rjl 4/11/2002 - param 2 was UW16 */
    UW16 (*make_gaso_and_stats) (FSP PBUCKET, PCHAR_STATS);
    UW16 (*render) (FSP PVOID, PBUCKET, INTR, INTR);
#if WIDTHS
   UW16 (*get_width) (FSP PBUCKET);
#endif /* WIDTHS */
} IF_FUNC_TBL;
typedef IF_FUNC_TBL FARPTR * PIF_FUNC_TBL;

EXTERN BOOLEAN if_init_glob(FSP PBUCKET);
EXTERN UW16   IXinit(FSP0);
EXTERN VOID    IXexit(FSP0);
EXTERN UW16   IXget_fnt_index(FSP PFONTCONTEXT);

/*
pointer callable UFST functions
*/
#if IF_RDR
EXTERN UW16 ifload_font (FSP PFONT_DES, PBUCKET);
EXTERN UW16 ifunload_font (FSP PBUCKET);
EXTERN UW16 ifset_trans (FSP PBUCKET, SL32, SL32, SL32, SL32);
EXTERN UW16 ifset_char (FSP PBUCKET, UL32); /* rjl 9/20/2002 - param 2 was UW16 */
EXTERN UW16 ifmake_gaso_and_stats (FSP PBUCKET, PCHAR_STATS);
EXTERN UW16 ifrender (FSP PVOID, PBUCKET, INTR, INTR);
#if WIDTHS
EXTERN UW16 ifget_width (FSP PBUCKET);
#endif /* WIDTHS */
#if FONTBBOX
EXTERN UW16 ifget_FontBBox( FSP PBUCKET, BBoxRes* );
#endif
#if FNT_METRICS
EXTERN UW16 if_get_FontMetrics( FSP PBUCKET, FONT_METRICS*, SL32 );
#endif
#endif  /* IF_RDR */

#if PST1_RDR
EXTERN UW16 psload_font (FSP PFONT_DES, PBUCKET);
EXTERN UW16 psunload_font (FSP PBUCKET);
EXTERN UW16 psset_trans (FSP PBUCKET, SL32, SL32, SL32, SL32);
EXTERN UW16 psset_char (FSP PBUCKET, UL32); /* rjl 9/20/2002 - param 2 was UW16 */
EXTERN UW16 psmake_gaso_and_stats (FSP PBUCKET, PCHAR_STATS);
EXTERN UW16 psrender (FSP PVOID, PBUCKET, INTR, INTR);
EXTERN VOID transfn8 (FSPvoid SL32, SL32, LPSL32, LPSL32);

#if FNT_METRICS
EXTERN UW16 ps_get_FontMetrics( FSP PBUCKET, FONT_METRICS*, SL32 );
#endif

EXTERN SL32 inithints(FSP0);
EXTERN VOID sort_hints(FSP0);

#if WIDTHS
EXTERN UW16 psget_width (FSP PBUCKET);
#endif /* WIDTHS */
#endif /* PST1_RDR */

#if TT_RDR
#if (TT_ROM || TT_DISK || TT_ROM_ACT)

#if UFST_MULTITHREAD
EXTERN UW16  CGENTRY CGIFtt_cmap_query (FSP UB8 *pFont, UW16 uTTCIndex, CMAP_QUERY *ret);
EXTERN UW16  CGENTRY CGIFtt_name_query (FSP UB8 *pFont, UW16 uTTCIndex, NAME_QUERY *ret);
#endif	/* UFST_MULTITHREAD */
/* also ... need to see the single thread versions, but they are not part of API */
EXTERN UW16  CGENTRY CGIFFtt_cmap_query (FSP UB8 *pFont, UW16 uTTCIndex, CMAP_QUERY *ret);
EXTERN UW16  CGENTRY CGIFFtt_name_query (FSP UB8 *pFont, UW16 uTTCIndex, NAME_QUERY *ret);
#if (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS
#if UFST_MULTITHREAD
EXTERN UW16  CGENTRY  CGIFtt_query_direct (FSP LPUB8, UL32, UW16, LPUL32, LPLPUB8);
EXTERN UW16  CGENTRY  CGIFtt_query_direct_free (FSP LPUB8 );
#endif	/* UFST_MULTITHREAD */
/* also ... need to see the single thread versions, but they are not part of API */
EXTERN UW16  CGENTRY  CGIFFtt_query_direct (FSP LPUB8, UL32, UW16, LPUL32, LPLPUB8);
EXTERN UW16  CGENTRY  CGIFFtt_query_direct_free (FSP LPUB8 );
EXTERN UW16 ttTBLinit (FSP0);
EXTERN VOID ttTBLexit (FSP0);
EXTERN UL32 tt_GetTagOffset(FSP LPUB8, UL32, UL32);
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_slot (FSP0);
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_font (FSP LPUB8, UL32, UW16);
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_table (FSP LPUB8);
#endif
#endif
EXTERN UW16  CGENTRY  CGIFtt_query( FSP LPUB8, UL32, UW16, LPUL32, LPUB8);
EXTERN UW16  CGENTRY  CGIFFtt_query( FSP LPUB8, UL32, UW16, LPUL32, LPUB8);
EXTERN UL32 tt_GetTagSize( FSP LPUB8, UL32, UL32);
EXTERN UW16 tt_GetTagBuffer( FSP LPUB8, UL32, UL32, UL32, LPUB8 );



EXTERN UW16 ttload_font (FSP PFONT_DES, PBUCKET);
EXTERN UW16 ttunload_font (FSP PBUCKET);
EXTERN UW16 ttset_trans (FSP PBUCKET, SL32, SL32, SL32, SL32);
EXTERN UW16 ttset_char (FSP PBUCKET, UL32); /* rjl 4/11/2002 - param 2 was UW16 */
EXTERN UW16 ttmake_gaso_and_stats (FSP PBUCKET, PCHAR_STATS);
EXTERN UW16 ttrender (FSP PVOID, PBUCKET, INTR, INTR);

EXTERN VOID normalize_16_16(FS_FIXED_POINT *);

#if WIDTHS
EXTERN UW16 ttget_width (FSP PBUCKET);
#endif /* WIDTHS */
#if FONTBBOX
EXTERN UW16 ttget_FontBBox( PBUCKET, BBoxRes* );
#endif
#if FNT_METRICS
EXTERN UW16 tt_get_FontMetrics( FSP PBUCKET, FONT_METRICS*, SL32 );
#endif
#endif /* TT_RDR */

#if FCO_RDR
EXTERN UW16 fco_load_font( FSP PFONT_DES, PBUCKET );
EXTERN UW16 fco_unload_font( FSP PBUCKET );
EXTERN UW16 fco_set_trans( FSP PBUCKET, SL32, SL32, SL32, SL32 );
EXTERN UW16 fco_set_char( FSP PBUCKET, UL32 );  /* rjl 9/6/2002 - was UW16*/
EXTERN UW16 fco_make_gaso_and_stats( FSP PBUCKET, PCHAR_STATS );
EXTERN UW16 fco_render( FSP PVOID, PBUCKET, INTR, INTR );
#if WIDTHS
EXTERN UW16 fco_get_width( FSP PBUCKET );
#endif /* WIDTHS */
#if FONTBBOX
EXTERN UW16 fco_get_FontBBox( FSP PBUCKET, SL32, BBoxRes* );
#endif
#if FNT_METRICS
EXTERN UW16 fco_get_FontMetrics( FSP PBUCKET, FONT_METRICS*, SL32 );
#endif
EXTERN VOID       fco_searchPathCur( FSP SL32*, FCTYPE**, SL32[] );
#if FCO2
EXTERN UW16       decode_stream ( FSP MODELTYPE*, SL32*,SL32*, SL32*);
#endif /* FCO2 */
#endif /* FCO_RDR */

EXTERN UW16 MTinit(FSP0);	/* 07-14-04 jfd */
#if UFST_MULTITHREAD
EXTERN UW16 remove_thread_id_from_list(FSP LPUL32);
EXTERN UW16 add_thread_id_to_list(FSP LPUL32);
EXTERN VOID print_bucket_status(FSP0);
EXTERN BOOLEAN thread_list_is_empty(FSP LPUL32);
EXTERN BOOLEAN only_thread_in_list(FSP LPUL32, UL32);
EXTERN VOID print_current_thread(FSP0);
#endif	/* UFST_MULTITHREAD */

#else  /* !LINT_ARGS */

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
typedef IF_FUNC_TBL FARPTR * PIF_FUNC_TBL;


EXTERN BOOLEAN if_init_glob();
EXTERN UW16   IXinit();
EXTERN VOID    IXexit();
EXTERN UW16   IXget_fnt_index();

/*
pointer callable URIP functions
*/
#if IF_RDR
EXTERN UW16 ifload_font ();
EXTERN UW16 ifunload_font ();
EXTERN UW16 ifset_trans ();
EXTERN UW16 ifset_char ();
EXTERN UW16 ifmake_gaso_and_stats ();
EXTERN UW16 ifrender ();
#if WIDTHS
EXTERN UW16 ifget_width ();
#endif /* WIDTHS */
#if FONTBBOX
EXTERN UW16 ifget_FontBBox();
#endif
#if FNT_METRICS
EXTERN UW16 if_get_FontMetrics();
#endif
#endif  /* IF_RDR */

#if PST1_RDR
EXTERN UW16 psload_font ();
EXTERN UW16 psunload_font ();
EXTERN UW16 psset_trans ();
EXTERN UW16 psset_char ();
EXTERN UW16 psmake_gaso_and_stats ();
EXTERN UW16 psrender ();
EXTERN VOID transfn8 ();

#if FNT_METRICS
EXTERN UW16 ps_get_FontMetrics();
#endif

EXTERN SL32 inithints();
EXTERN VOID sort_hints();
#if WIDTHS
EXTERN UW16 psget_width ();
#endif /* WIDTHS */
#endif /* PST1_RDR */

#if TT_RDR
#if (TT_ROM || TT_DISK || TT_ROM_ACT)
#if UFST_MULTITHREAD
EXTERN UW16  CGENTRY CGIFtt_cmap_query ();
EXTERN UW16  CGENTRY CGIFtt_name_query ();
#endif
/* also ... need to see the single thread versions, but they are not part of API */
EXTERN UW16  CGENTRY CGIFFtt_cmap_query ();
EXTERN UW16  CGENTRY CGIFFtt_name_query ();



#if (CACHE || CHAR_HANDLE) && DIRECT_TT_TABLE_ACCESS
#if UFST_MULTITHREAD
EXTERN UW16  CGENTRY  CGIFtt_query_direct ();
EXTERN UW16  CGENTRY  CGIFtt_query_direct_free ();
#endif
/* also ... need to see the single thread versions, but they are not part of API */
EXTERN UW16  CGENTRY  CGIFFtt_query_direct ();
EXTERN UW16  CGENTRY  CGIFFtt_query_direct_free ();


EXTERN UW16 ttTBLinit ();
EXTERN VOID ttTBLexit ();
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_slot ();
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_font ();
EXTERN DIRECT_ACCESS_ENTRY * ttTBL_find_table ();
EXTERN UL32 tt_GetTagOffset();
#endif
#endif
EXTERN UW16  CGENTRY  CGIFtt_query();
EXTERN UL32 tt_GetTagSize();
EXTERN UW16 tt_GetTagBuffer();



EXTERN UW16 ttload_font ();
EXTERN UW16 ttunload_font ();
EXTERN UW16 ttset_trans ();
EXTERN UW16 ttset_char ();
EXTERN UW16 ttmake_gaso_and_stats ();
EXTERN UW16 ttrender ();

EXTERN VOID normalize_16_16();

#if WIDTHS
EXTERN UW16 ttget_width ();
#endif /* WIDTHS */
#if FONTBBOX
EXTERN UW16 ttget_FontBBox();
#endif
#if FNT_METRICS
EXTERN UW16 tt_get_FontMetrics();
#endif
#endif /* TT_RDR */

#if FCO_RDR
EXTERN UW16 fco_load_font();
EXTERN UW16 fco_unload_font();
EXTERN UW16 fco_set_trans();
EXTERN UW16 fco_set_char();
EXTERN UW16 fco_make_gaso_and_stats();
EXTERN UW16 fco_render();
#if WIDTHS
EXTERN UW16 fco_get_width();
#endif /* WIDTHS */
#if FONTBBOX
EXTERN UW16 fco_get_FontBBox();
#endif
#if FNT_METRICS
EXTERN UW16 fco_get_FontMetrics();
#endif
EXTERN VOID       fco_searchPathCur();
#if FCO2
EXTERN UW16       decode_stream ();
#endif /* FCO2 */
#endif /* FCO_RDR */

EXTERN UW16 MTinit();	/* 07-14-04 jfd */
#if UFST_MULTITHREAD
EXTERN UW16 remove_thread_id_from_list();
EXTERN UW16 add_thread_id_to_list();
EXTERN VOID print_bucket_status();
EXTERN BOOLEAN thread_list_is_empty();
EXTERN BOOLEAN only_thread_in_list();
EXTERN VOID print_current_thread();
#endif
#endif /* LINT_ARGS */
/* end of prototypes moved from ix.h */


/* Moved from cgif.h */
#ifdef LINT_ARGS
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFconfig(FSP PIFCONFIG);

UFST_EXTERNAL_INTERFACE SL32 CGENTRY varmul(SL32, SL32, INTG);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY varmul_64(FS_FIXED, FS_FIXED, INTG);
UFST_EXTERNAL_INTERFACE FS_FIXED CGENTRY varmul_asm(FS_FIXED, FS_FIXED,INTG);

UFST_EXTERNAL_INTERFACE FS_FIXED CGENTRY vardiv(FS_FIXED, FS_FIXED, INTG);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY vardiv_64(SL32, SL32, INTG);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY vardiv_asm(SL32, SL32, INTG);

UFST_EXTERNAL_INTERFACE SL32 CGENTRY muldiv(SL32, SL32, SL32);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY muldiv_64(SL32, SL32, SL32);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY muldiv_asm(SL32,SL32,SL32);
#if (HUGE_PTR_SUPPORT)
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFfund  (FSP UW16, HPSB8, UL32, LPUW16);
EXTERN  UW16  CGENTRY  CGIFmove_block (FSP UW16, HPSB8);
#else
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFfund  (FSP UW16, LPSB8, UL32, LPUW16);
EXTERN  UW16  CGENTRY  CGIFmove_block (FSP UW16, LPSB8);
#endif
UFST_EXTERNAL_INTERFACE  SW16         CGIFinitRomInfo(FSP LPUB8, LPUB8*);
#if TT_TTPLUG   /* TrueType uses TrueType Universal Plugins */
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFtt_universal(FSP LPUB8, SL32);
#endif
#if FONTBBOX
EXTERN  UW16  CGENTRY  CGIFbbox_IFPCLEOchar(FSP LPUB8, LPUB8, LPUW16);
#endif
#if FCO_RDR
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFfco_Plugin( FSP SW16 );
#endif
EXTERN  UW16  CGENTRY  CGIFchIdptr( FSP VOID *, VOID * );
EXTERN  VOID  CGENTRY  CGIFfont_access( FSP UW16 );
EXTERN VOID CGENTRY CGIFwhat_version(UFST_VERSION_INFO *);

#else   /* ! LINT_ARGS */

EXTERN  UW16  CGENTRY  CGIFconfig();
EXTERN  UW16  CGENTRY  CGIFfund();
EXTERN  SL32 CGENTRY varmul();
EXTERN  SL32 CGENTRY varmul_64();
EXTERN  FS_FIXED CGENTRY varmul_asm();

EXTERN  FS_FIXED CGENTRY vardiv();
EXTERN  SL32 CGENTRY vardiv_64();
EXTERN  SL32 CGENTRY vardiv_asm();

EXTERN  SL32 CGENTRY muldiv();
EXTERN  SL32 CGENTRY muldiv_64();
EXTERN  SL32 CGENTRY muldiv_asm();
EXTERN  UW16  CGENTRY  CGIFmove_block();

EXTERN  SW16         CGIFinitRomInfo();
#if TT_TTPLUG   /* TrueType uses TrueType Universal Plugins */
EXTERN  UW16  CGENTRY  CGIFtt_universal();
#endif
#if FONTBBOX
EXTERN  UW16  CGENTRY  CGIFbbox_IFPCLEOchar();
#endif
#if FCO_RDR
EXTERN  UW16  CGENTRY  CGIFfco_Plugin();
#endif
EXTERN  UW16  CGENTRY  CGIFchIdptr();
EXTERN  VOID  CGENTRY  CGIFfont_access();
EXTERN VOID CGENTRY CGIFwhat_version();
#endif /* LINT_ARGS */
/* end of prototypes moved from cgif.h */

#if UFST_MULTITHREAD
#ifdef LINT_ARGS
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFinit  (FSP0);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFenter (FSP0);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFexit  (FSP0);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFfont  (FSP PFONTCONTEXT);
#if WIDTH_NOSYMMAP
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFwidth (FSP PWIDTH_LIST_INPUT_ENTRY, UW16, UW16, LPUW16);
#else
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFwidth (FSP UL32, UW16, UW16, LPUW16);
#endif
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFchar  (FSP UL32, PPIFBITMAP, SW16); /* rjl 6/5/2002 - param 1 was UW16*/
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFchar_by_ref  (FSP UL32, PPIFBITMAP, SW16);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFfree_by_ref  (FSP PIFBITMAP);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFfont_purge(FSP PFONTCONTEXT);
UFST_EXTERNAL_INTERFACE  UL32  CGENTRY  CGIFbucket_purge(FSP UL32);
UFST_EXTERNAL_INTERFACE  UL32  CGENTRY  CGIFmem_purge(FSP UL32);
EXTERN  UW16  CGENTRY  CGIFhdr_font_purge(FSP PFONTCONTEXT);
EXTERN  UW16  CGENTRY  CGIFtile(FSP PIFTILE, PPIFBITMAP);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFchar_handle(FSP UL32, PHIFBITMAP, SW16);  /* rjl 4/10/2002 - param 1 was UW16*/
EXTERN  UW16  CGENTRY  CGIFtile_handle(FSP PIFTILE, PHIFBITMAP);
#if (MAX_BM_BITS > 16)
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFchar_size(FSP UL32, LPSL32, SW16);  /* rjl 9/18/2002 - param 1 was UW16*/
#else
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFchar_size(FSP UL32, LPUW16, SW16);  /* rjl 9/20/2002 - param 2 was UW16 */
#endif
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFmakechar(FSP PIFBITMAP, LPUB8);
EXTERN  UW16  CGENTRY  CGIFbmheader(FSP UL32, PIFBITMAP, SW16); /* rjl 9/6/2002 - param 1 was UW16*/
EXTERN  UW16  CGENTRY  CGIFtilebitMap(FSP PIFTILE, PIFBITMAP, LPUB8);
EXTERN  UW16  CGENTRY  CGIFdefund(FSP UW16);
EXTERN  UW16  CGENTRY  CGIFsegments(FSP SL32, UW16, LPUW16, LPSB8);
#if FONTBBOX
#if VLCOUTPUT
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFbound_box( FSP SL32*, LPSW16 );
#else
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFbound_box( FSP SL32* );
#endif
#endif
#if FCO_RDR
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFfco_Open( FSP LPUB8, LPSW16);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFfco_Close( FSP SW16 );
#endif
#if FCOACCESS
EXTERN  UW16  CGENTRY  CGIFfco_Access( FSP LPUB8, UW16, UW16, LPUW16, LPSB8 );
#endif /* FCOACCESS */
#if FNT_METRICS
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFfont_metrics( FSP FONT_METRICS* );
#endif  /* FNT_METRICS */

UFST_EXTERNAL_INTERFACE MEM_HANDLE CGENTRY CGIFnew_client(/* FSP PIFCONFIG */ FSP0);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY CGIFend_client(FSP MEM_HANDLE);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY CGIFcreate_mutex(FSP0);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY CGIFobtain_mutex(FSP0);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY CGIFrelease_mutex(FSP0);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY CGIFdelete_mutex(FSP0);

UFST_EXTERNAL_INTERFACE SL32 CGENTRY CGIFcreate_app_mutex(FSP0);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY CGIFobtain_app_mutex(FSP0);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY CGIFrelease_app_mutex(FSP0);
UFST_EXTERNAL_INTERFACE SL32 CGENTRY CGIFdelete_app_mutex(FSP0);

UFST_EXTERNAL_INTERFACE VOID CGENTRY CGIFCHARfree(FSP MEM_HANDLE);

UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFtile_nzw_buf(FSP PIFTILE piftile, LPSL32 psize);

#if TT_RDR
UFST_EXTERNAL_INTERFACE UW16 CGIFchar_get_gpos_pts(FSP UW16, UW16, UW16 *, SL32 *, SL32 *);
UFST_EXTERNAL_INTERFACE UW16 CGIFchar_map  (FSP UL32, UW16 *);
UFST_EXTERNAL_INTERFACE SW16 CGIFget_kern_value(FSP UB8 *tableBuffer, UL32 id1, UL32 id2);
#endif

EXTERN  SL32  CGENTRY  get_thread_id(FSP0);
#else	/* !LINT_ARGS */
EXTERN  UW16  CGENTRY  CGIFinit();
EXTERN  UW16  CGENTRY  CGIFenter();
EXTERN  UW16  CGENTRY  CGIFexit();
EXTERN  UW16  CGENTRY  CGIFfont();
EXTERN  UW16  CGENTRY  CGIFwidth();

EXTERN  UW16  CGENTRY  CGIFchar();
EXTERN  UW16  CGENTRY  CGIFchar_by_ref();
EXTERN  UW16  CGENTRY  CGIFfree_by_ref();
EXTERN  UW16  CGENTRY  CGIFfont_purge();
EXTERN  UL32  CGENTRY  CGIFbucket_purge();
EXTERN  UL32  CGENTRY  CGIFmem_purge();
EXTERN  UW16  CGENTRY  CGIFhdr_font_purge();
EXTERN  UW16  CGENTRY  CGIFtile();
EXTERN  UW16  CGENTRY  CGIFchar_handle();
EXTERN  UW16  CGENTRY  CGIFtile_handle();
EXTERN  UW16  CGENTRY  CGIFchar_size();
EXTERN  UW16  CGENTRY  CGIFmakechar();
EXTERN  UW16  CGENTRY  CGIFbmheader();
EXTERN  UW16  CGENTRY  CGIFtilebitMap();
EXTERN  UW16  CGENTRY  CGIFdefund();
EXTERN  UW16  CGENTRY  CGIFsegments();
EXTERN  UW16  CGENTRY  CGIFbound_box();
#if FCO_RDR
EXTERN  UW16  CGENTRY  CGIFfco_Open();
EXTERN  UW16  CGENTRY  CGIFfco_Close();
EXTERN  UW16  CGENTRY  CGIFfco_Plugin();
#endif	
#if FCOACCESS
EXTERN  UW16  CGENTRY  CGIFfco_Access();
#endif /* FCOACCESS */
#if FNT_METRICS
EXTERN  UW16  CGENTRY  CGIFfont_metrics();
#endif  /* FNT_METRICS */

EXTERN  MEM_HANDLE CGENTRY CGIFnew_client();
EXTERN  SL32 CGENTRY CGIFend_client();
EXTERN  SL32 CGENTRY CGIFcreate_mutex();
EXTERN  SL32 CGENTRY CGIFobtain_mutex();
EXTERN  SL32 CGENTRY CGIFrelease_mutex();
EXTERN  SL32 CGENTRY CGIFdelete_mutex();

EXTERN  SL32 CGENTRY CGIFcreate_app_mutex();
EXTERN  SL32 CGENTRY CGIFobtain_app_mutex();
EXTERN  SL32 CGENTRY CGIFrelease_app_mutex();
EXTERN  SL32 CGENTRY CGIFdelete_app_mutex();

EXTERN  VOID CGENTRY CGIFCHARfree();

EXTERN  UW16 CGENTRY CGIFtile_nzw_buf();

#if TT_RDR
EXTERN  UW16 CGIFchar_get_gpos_pts();
EXTERN  UW16 CGIFchar_map();
#endif

#if TT_RDR
UFST_EXTERNAL_INTERFACE SW16 CGIFget_kern_value();
#endif
EXTERN  SL32  CGENTRY  get_thread_id();

#endif	/* LINT_ARGS */
#endif	/* UFST_MULTITHREAD */

/* single threaded versions - one of two versions if MULTITHREAD is defined */
#ifdef LINT_ARGS
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFinit  (FSP0);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFenter (FSP0);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFexit  (FSP0);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFfont  (FSP PFONTCONTEXT);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFchar  (FSP UL32, PPIFBITMAP, SW16); /* rjl 6/5/2002 - param 1 was UW16*/
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFchar_by_ref  (FSP UL32, PPIFBITMAP, SW16);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFfree_by_ref  (FSP PIFBITMAP);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFfont_purge(FSP PFONTCONTEXT);
UFST_EXTERNAL_INTERFACE  UL32  CGENTRY  CGIFFbucket_purge(FSP UL32);
UFST_EXTERNAL_INTERFACE  UL32  CGENTRY  CGIFFmem_purge(FSP UL32);
EXTERN  UW16  CGENTRY  CGIFFhdr_font_purge(FSP PFONTCONTEXT);
EXTERN  UW16  CGENTRY  CGIFFtile(FSP PIFTILE, PPIFBITMAP);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFchar_handle(FSP UL32, PHIFBITMAP, SW16);  /* rjl 4/10/2002 - param 1 was UW16*/
EXTERN  UW16  CGENTRY  CGIFFtile_handle(FSP PIFTILE, PHIFBITMAP);
#if (MAX_BM_BITS > 16)
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFchar_size(FSP UL32, LPSL32, SW16);  /* rjl 9/18/2002 - param 1 was UW16*/
#else
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFchar_size(FSP UL32, LPUW16, SW16);  /* rjl 9/20/2002 - param 2 was UW16 */
#endif
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFmakechar(FSP PIFBITMAP, LPUB8);
EXTERN  UW16  CGENTRY  CGIFFbmheader(FSP UL32, PIFBITMAP, SW16); /* rjl 9/6/2002 - param 1 was UW16*/

EXTERN  UW16  CGENTRY  CGIFFtilebitMap(FSP PIFTILE, PIFBITMAP, LPUB8);
EXTERN  UW16  CGENTRY  CGIFFdefund(FSP UW16);
EXTERN  UW16  CGENTRY  CGIFFsegments(FSP SL32, UW16, LPUW16, LPSB8);
#if FONTBBOX
#if VLCOUTPUT
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFbound_box( FSP SL32*, LPSW16 );
#else
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFbound_box( FSP SL32* );
#endif
#endif
#if FCO_RDR
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFfco_Open( FSP LPUB8, LPSW16);
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFfco_Close( FSP SW16 );
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFfco_Plugin( FSP SW16);
#endif
#if FCOACCESS
EXTERN  UW16  CGENTRY  CGIFFfco_Access( FSP LPUB8, UW16, UW16, LPUW16, LPSB8 );
#endif /* FCOACCESS */
#if FNT_METRICS
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFfont_metrics( FSP FONT_METRICS* );
#endif  /* FNT_METRICS */
#if WIDTH_NOSYMMAP
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFwidth (FSP PWIDTH_LIST_INPUT_ENTRY, UW16, UW16, LPUW16);
#else
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFwidth (FSP UL32, UW16, UW16, LPUW16);
#endif

#if CHAR_SIZE && TILE && NON_Z_WIND
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFtile_nzw_buf(FSP PIFTILE piftile, LPSL32 psize);
#endif

#if TT_RDR
UFST_EXTERNAL_INTERFACE UW16 CGIFFchar_get_gpos_pts(FSP UW16, UW16, UW16 *, SL32 *, SL32 *);
UFST_EXTERNAL_INTERFACE UW16 CGIFFchar_map  (FSP UL32, UW16 *);
UFST_EXTERNAL_INTERFACE SW16 CGIFFget_kern_value(FSP UB8 *tableBuffer, UL32 id1, UL32 id2);
#endif

#else	/* !LINT_ARGS */
EXTERN  UW16  CGENTRY  CGIFFinit();
EXTERN  UW16  CGENTRY  CGIFFenter();
EXTERN  UW16  CGENTRY  CGIFFexit();
EXTERN  UW16  CGENTRY  CGIFFfont();
EXTERN  UW16  CGENTRY  CGIFFchar();
EXTERN  UW16  CGENTRY  CGIFFchar_by_ref();
EXTERN  UW16  CGENTRY  CGIFFfree_by_ref();
EXTERN  UW16  CGENTRY  CGIFFfont_purge();
EXTERN  UL32  CGENTRY  CGIFFbucket_purge();
EXTERN  UL32  CGENTRY  CGIFFmem_purge();
EXTERN  UW16  CGENTRY  CGIFFhdr_font_purge();
EXTERN  UW16  CGENTRY  CGIFFtile();
EXTERN  UW16  CGENTRY  CGIFFchar_handle();
EXTERN  UW16  CGENTRY  CGIFFtile_handle();
EXTERN  UW16  CGENTRY  CGIFFchar_size();
EXTERN  UW16  CGENTRY  CGIFFmakechar();
EXTERN  UW16  CGENTRY  CGIFFbmheader();
EXTERN  UW16  CGENTRY  CGIFFtilebitMap();
EXTERN  UW16  CGENTRY  CGIFFdefund();
EXTERN  UW16  CGENTRY  CGIFFsegments();
EXTERN  UW16  CGENTRY  CGIFFbound_box();
#if FCO_RDR
EXTERN  UW16  CGENTRY  CGIFFfco_Open();
EXTERN  UW16  CGENTRY  CGIFFfco_Close();
EXTERN  UW16  CGENTRY  CGIFFfco_Plugin();
#endif
#if FCOACCESS
EXTERN  UW16  CGENTRY  CGIFFfco_Access();
#endif /* FCOACCESS */
#if FNT_METRICS
EXTERN  UW16  CGENTRY  CGIFFfont_metrics();
#endif  /* FNT_METRICS */
EXTERN  UW16  CGENTRY  CGIFFwidth();
#if CHAR_SIZE && TILE && NON_Z_WIND
UFST_EXTERNAL_INTERFACE  UW16  CGENTRY  CGIFFtile_nzw_buf();
#endif


#if TT_RDR
UFST_EXTERNAL_INTERFACE UW16 CGIFFchar_get_gpos_pts();
UFST_EXTERNAL_INTERFACE UW16 CGIFFchar_map();
UFST_EXTERNAL_INTERFACE SW16 CGIFFget_kern_value();
#endif

#endif /* LINT_ARGS */

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

#define CGIFCHARfree CHARfree

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

#define CGIFinitJisToUnicodeTbl CGIFFinitJisToUnicodeTbl
#define CGIFinitKscToUnicodeTbl CGIFFinitKscToUnicodeTbl
#define CGIFinitBig5ToUnicodeTbl CGIFFinitBig5ToUnicodeTbl
#define CGIFinitGbToUnicodeTbl CGIFFinitGbToUnicodeTbl

#endif	/* !UFST_MULTITHREAD */


/**** NEW SECTION start ****/

#ifdef LINT_ARGS
EXTERN UW16 DoEffects( FSP PIFBITMAP );
EXTERN SL32 HowMuchBiggerIsEffect(FSP UW16 num_loop, SW16 x_dim, SW16 y_dim);
#else
EXTERN UW16 DoEffects();
EXTERN SL32 HowMuchBiggerIsEffect();
#endif

#if PCLEO_RDR	/* core code for PCLEO processing */
#ifdef LINT_ARGS
EXTERN UW16      PCLload_font(FSP LPUB8, PBUCKET);
EXTERN UW16      PCLEOmake_cd(FSP UW16, PCHR_DEF, SL32);
EXTERN VOID		 getHPFH( PHPFH, LPUB8 );
EXTERN LPUB8	 PCLchId2ptr (FSP UW16);
EXTERN UW16 	 PCLswapHdr(FSP LPUB8, UW16);
EXTERN UW16		 PCLswapChar(FSP LPUB8);

#if TT_PCLEOI
EXTERN LPUB8 PCLglyphID2Ptr( FSP UW16 );
#endif

EXTERN UW16    PCLeo2IF_ssnum( FSP PFONTCONTEXT, UW16, LPUW16 );

#else
EXTERN UW16      PCLload_font();
EXTERN UW16      PCLEOmake_cd();
EXTERN VOID		 getHPFH();
EXTERN LPUB8	 PCLchId2ptr ();
EXTERN UW16 	 PCLswapHdr();
EXTERN UW16		 PCLswapChar();
#if TT_PCLEOI
EXTERN LPUB8 PCLglyphID2Ptr();
#endif
EXTERN UW16    PCLeo2IF_ssnum();
#endif	/* LINT_ARGS */
#endif	/* PCLEO_RDR */


#if PCLEO_RDR	/* demo code for PCLEO processing */
#ifdef LINT_ARGS
EXTERN SW16   installPcleo( FSP SB8 * );
EXTERN LPUB8                lastPcleoFontHdr( FSP0 );
EXTERN VOID init_pcleo_callback_data( FSP0 );

#else  /* !LINT_ARGS */
EXTERN SW16   installPcleo();
EXTERN LPUB8                lastPcleoFontHdr();
EXTERN VOID init_pcleo_callback_data();

#endif  /* LINT_ARGS */
#endif	/* PCLEO_RDR */


#if (CGBITMAP || GRAYSCALING)

#ifdef WINDCOMP
EXTERN NZTRANS *new_trans(FSP SW16);
EXTERN UW16 add_trans(FSP NZTRANS*, FS_FIXED, SW16);
EXTERN VOID delete_trans(FSP NZTRANS*);
#endif /* WINDCOMP */

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
EXTERN UW16   bmras_quadto(FSPvoid PVOID, INTR, INTR, INTR, INTR);
#if (STIK && FS_DIRECT)
EXTERN void   draw_quad(FSP FS_FIXED, FS_FIXED,FS_FIXED, FS_FIXED,FS_FIXED, FS_FIXED);
EXTERN void   draw_line(FSP FS_FIXED,FS_FIXED,FS_FIXED,FS_FIXED);
#endif
#if PST1_RDR
EXTERN UW16   bmras_cubeto (FSPvoid PVOID, INTR, INTR, INTR, INTR, INTR, INTR);
#endif

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
EXTERN UW16   bmras_quadto();
#if (STIK && FS_DIRECT)
EXTERN void   draw_quad();
EXTERN void   draw_line();
#endif
#if PST1_RDR
EXTERN UW16   bmras_cubeto ();
#endif
#endif	/* LINT_ARGS */
#endif	/* (CGBITMAP || GRAYSCALING) */

/* COPIED from bitmap.c */
#ifdef LINT_ARGS
#if CHAR_SIZE
EXTERN VOID   Char_sz_space_metrics(FSP PIFBITMAP);
#endif
#if CGBITMAP
EXTERN UW16      outbuffer(FSP PIFOUTLINE);
#ifdef AGFADEBUG
EXTERN VOID      pr_tran(LPUB8, SW16, SW16);
#endif
#endif 	/* CGBITMAP	*/

#else
#if CHAR_SIZE
EXTERN VOID   Char_sz_space_metrics();
#endif
#if CGBITMAP
EXTERN UW16      outbuffer();
#ifdef AGFADEBUG
EXTERN VOID      pr_tran();
#endif
#endif 	/* CGBITMAP	*/
#endif  /* LINT_ARGS */

/* COPIED from maker.c */
#ifdef LINT_ARGS
EXTERN UW16    map_and_set_scale(FSP PFONTCONTEXT, SL32);   /* rjl 4/10/2002 - param 2 was SW16 */
EXTERN SW16    frac_bits(SL32);
#if CGBITMAP || GRAYSCALING
EXTERN VOID    bmp_setRender(FSP0);
#endif
#if OUTLINE
EXTERN VOID    out_setRender(FSP0);
#endif  /* OUTLINE */
#if BOLD_P6 || BOLD_HORIZONTAL || BOLD_VERTICAL 
EXTERN VOID smear_hookRender( PSMEAR_INSTANCE, PPVOID, POUT_TBL );
EXTERN UW16	smear_calcvec( FSP UW16, PSL32VECTOR, PSL32VECTOR );
#endif
#if BOLD_HORIZONTAL
EXTERN UW16   smear_setboldhorizontal ( FSP UW16, PSMEAR_INSTANCE );
#endif
#if BOLD_VERTICAL
EXTERN UW16   smear_setboldvertical ( FSP UW16, PSMEAR_INSTANCE ); /* keb for lex */
#endif
#if BOLD_P6
EXTERN UW16   smear_setboldp6 ( FSP UW16, PSMEAR_INSTANCE, PSMEAR_INSTANCE );
#endif

#else          /* NOT LINT_ARGS */
EXTERN UW16    map_and_set_scale();
EXTERN SW16    frac_bits();
#if CGBITMAP || GRAYSCALING
EXTERN VOID    bmp_setRender();
#endif
#if OUTLINE
EXTERN VOID    out_setRender();
#endif  /* OUTLINE */
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
#endif         /* LINT_ARGS  */

/* COPIED from makechar.c */
#ifdef LINT_ARGS
#if LINKED_FONT
EXTERN VOID LFNT_SetFONTCONTEXT(FSP PFONTCONTEXT fc, UL32 fontNumber);
EXTERN UW16 LFNT_MAKifbmheader(FSP PFONTCONTEXT, SL32, PIFBITMAP);
#endif
EXTERN UW16    MAKifbmheader(FSP PFONTCONTEXT, SL32, PIFBITMAP); /* rjl 4/10/2002 - param 2 was SW16 */
EXTERN UW16    bitmap(FSP PHIFBITMAP);
EXTERN UW16    outdata(FSP PHIFOUTLINE);
EXTERN UW16    app_make_character( PFONTCONTEXT, UW16, PHIFBITMAP );
#else
#if LINKED_FONT
EXTERN VOID LFNT_SetFONTCONTEXT();
EXTERN UW16 LFNT_MAKifbmheader();
#endif
EXTERN UW16    MAKifbmheader();
EXTERN UW16    bitmap();
EXTERN UW16    outdata();
EXTERN UW16    app_make_character();
#endif

/* COPIED from mapfont.c */
#ifdef LINT_ARGS
EXTERN UW16   comp_pix(FSP PFONTCONTEXT, PBUCKET);
EXTERN UW16   IXsetup_chr_def(FSP UL32, PCHR_DEF, PPBUCKET);    /* rjl 4/10/2002 - param 1 was UW16 */
#else         /* NOT LINT_ARGS */
EXTERN UW16   comp_pix();
EXTERN UW16   IXsetup_chr_def();
#endif        /* LINT_ARGS  */

/* COPIED from cgif.c */
#ifdef LINT_ARGS
#if FONTBBOX
#if IF_PCLEOI
EXTERN LPUB8 PCLEO_charptr(FSP LPUB8, UW16);
#endif  /* IF_PCLEOI */
#endif  /* FONTBBOX */

#if CACHE || CHAR_HANDLE 
EXTERN UW16    makechar(FSP PFONTCONTEXT, SL32, PHIFBITMAP);    /* rjl 4/10/2002 - param 2 was SW16 */
#endif
EXTERN UW16    MAKfontSize(FSP PFONTCONTEXT, SL32, LPSL32);     /* rjl 9/18/2002 - param 2 was SW16*/

#if TILE  
EXTERN UW16    bitmap_tile(FSP PIFTILE, PHIFBITMAP);
#endif  /* 06-10-91 jfd */
EXTERN UW16    chwidth(FSP UL32);

#if FCO_RDR
EXTERN UW16  FCOinit( FSP0 );
EXTERN UW16  FCOexit( FSP0 );
EXTERN UW16  fco_InitFCO( FSP SL32*, LPUB8 );
EXTERN UW16  fco_DeleteFCO( FSP SL32 );
EXTERN UW16  fco_saveHndl( FSP SL32 );
EXTERN UW16  fco_GetMemHandle( FSP SL32, PMEM_HANDLE );
#endif

#if TT_RDR
EXTERN VOID set_plat_spec_lang(FSP UW16 *plat, UW16 *spec, UW16 *lang, UW16* cmap_ptr, UW16* name_ptr);
EXTERN UW16  ttkanset_char (FSP PBUCKET b, UL32 chId, fs_GlyphInputType *ttInput);
#endif

#else   /*  not LINTARGS  */

#if FONTBBOX
#if IF_PCLEOI
EXTERN LPUB8 PCLEO_charptr();
#endif  /* IF_PCLEOI */
#endif  /* FONTBBOX  */


#if CACHE || CHAR_HANDLE 
EXTERN UW16    makechar();
#endif
EXTERN UW16    MAKfontSize();

#if TILE
EXTERN UW16    bitmap_tile();
#endif
EXTERN UW16    chwidth();

#if FCO_RDR
EXTERN UW16  FCOinit();
EXTERN UW16  FCOexit();
EXTERN UW16  fco_InitFCO();
EXTERN UW16  fco_DeleteFCO();
EXTERN UW16  fco_GetMemHandle();
EXTERN UW16  fco_saveHndl();
#endif	/* FCO_RDR   */

#if TT_RDR
EXTERN VOID	 set_plat_spec_lang();
EXTERN UW16  ttkanset_char ();
#endif

#endif /* LINT_ARGS */

/* COPIED from fc_if.c */
#if FCO_RDR
#ifdef LINT_ARGS
EXTERN VOID       BUFpurgeBUCK(FSP0);
#else
EXTERN VOID       BUFpurgeBUCK();
#endif
#endif	/* FCO_RDR */

/* COPIED from ix.c */
#ifdef LINT_ARGS
EXTERN UW16         BUCKinit(FSP0);
EXTERN VOID         BUCKexit(FSP0);
EXTERN VOID         BUCKfree(FSP HBUCKET);   /* jwd, 11/18/02 */
EXTERN UW16 CACfont_free(FSP HIFFONT);
#else  /*  not LINTARGS  */
EXTERN UW16         BUCKinit();
EXTERN VOID         BUCKexit();
EXTERN VOID         BUCKfree();
EXTERN UW16 CACfont_free();
#endif    /* LINT_ARGS  */

/* COPIED from symmap.c */
#ifdef LINT_ARGS
#if USING_16_BIT_DOS		/* 16 bit platform */
EXTERN BOOLEAN     FMseek_read(FSP SL32, SL32, UW16, LPSB8);
EXTERN MEM_HANDLE  FMalloc_rd(FSP SL32, UW16, SL32,BOOLEAN);
#else 						/* 32 bit platform */
EXTERN BOOLEAN     FMseek_read(FSP SL32, SL32, SL32, LPSB8);
EXTERN MEM_HANDLE  FMalloc_rd(FSP SL32, SL32, SL32,BOOLEAN);
#endif
EXTERN VOID        buildpath(LPSB8, LPSB8, LPSB8);

#else
EXTERN BOOLEAN     FMseek_read();
EXTERN MEM_HANDLE  FMalloc_rd();
EXTERN VOID        buildpath();
#endif

/* COPIED from outdata.c */
#ifdef LINT_ARGS
EXTERN UW16      cgoutline(FSP0);
#else  /*  not LINTARGS  */
EXTERN UW16      cgoutline();
#endif    /* LINT_ARGS  */

/* COPIED from outline.c */
#ifdef LINT_ARGS
EXTERN UW16      quadra(FSP UW16, INTR, INTR);
EXTERN UW16      cubic(FSP INTR, INTR);
#else
EXTERN UW16      quadra();
EXTERN UW16      cubic();
#endif

/* COPIED from cubic.c */
#ifdef LINT_ARGS
EXTERN VOID  if_long_mult(SL32, SW16, LPSL32, LPSW16);
#else
EXTERN VOID  if_long_mult();
#endif

/* COPIED from bmputl.c */
#ifdef LINT_ARGS
EXTERN UW16   DArd_char(FSP PCHR_DEF);
#else 
EXTERN UW16   DArd_char();
#endif

/* COPIED from bucket.c */
#ifdef LINT_ARGS
#if PST1_RDR || CFF_RDR
EXTERN BOOLEAN     ps_is_same(FSP PBUCKET);
#endif
EXTERN PINDEX_ENTRY FIentry(FSP SL32);
#if DISK_FONTS
EXTERN LPSB8       FIpath(FSP PINDEX_ENTRY);
#endif 
EXTERN UW16        IXset_search_path(FSP SW16);
#if FCO_RDR
EXTERN VOID        fco_pluginSearchPath( FSP PBUCKET, LPUB8, SL32* );
#endif

#else    /*  NOT LINT_ARGS  */
#if PST1_RDR || CFF_RDR
EXTERN BOOLEAN     ps_is_same();
#endif
EXTERN PINDEX_ENTRY FIentry();
#if DISK_FONTS
EXTERN LPSB8       FIpath();
#endif 
EXTERN UW16        IXset_search_path();
#if FCO_RDR
EXTERN VOID        fco_pluginSearchPath();
#endif
#endif

/* COPIED from chr_def.c */
#ifdef LINT_ARGS
EXTERN UW16 DAmake_cd(FSP PBUCKET, UL32, PCHR_DEF, SL32);   /* rjl 4/10/2002 - param 2 was UW16 */
#if (IF_RDR || PST1_RDR)
EXTERN UW16  fco_map_cgnum_to_unicode( FSP UW16 );
#endif
#else
EXTERN UW16 DAmake_cd();
#if (IF_RDR || PST1_RDR)
EXTERN UW16  fco_map_cgnum_to_unicode();
#endif
#endif

/* COPIED from da.c */
#if IF_RDR
#ifdef LINT_ARGS
EXTERN UW16       LIBmake_cd(FSP PBUCKET, UW16, PCHR_DEF, SL32); /* jwd 20-Dec-98 */
#else /* not LINTARGS */
EXTERN UW16       LIBmake_cd();
#endif    /* LINT_ARGS  */
#endif  /* IF_RDR */

/* COPIED from fm.c */
#ifdef LINT_ARGS
EXTERN UW16      IXmak_font_index(FSP LPSB8);
EXTERN UW16      BUCKfind(FSP SL32, SL32, PPBUCKET);
#if DISK_FONTS
EXTERN UW16      IXopen_file(FSP PINDEX_ENTRY, PBUCKET);
EXTERN VOID      IXclose_file(FSP PBUCKET);
#endif

#else             /* NOT LINT_ARGS */
EXTERN UW16      IXmak_font_index();
EXTERN UW16      BUCKfind();
#if DISK_FONTS
EXTERN UW16      IXopen_file();
EXTERN VOID      IXclose_file();
#endif
#endif

#ifdef LINT_ARGS
GLOBAL VOID dd_open(PBUCKET pb);
GLOBAL SL32 dd_read(PBUCKET pb, VOID* buff, UL32 count);
GLOBAL SL32 dd_lseek(PBUCKET pb, SL32 pos, SL32 type);
GLOBAL VOID dd_close(PBUCKET pb);
#else
GLOBAL VOID dd_open();
GLOBAL SL32 dd_read();
GLOBAL SL32 dd_lseek();
GLOBAL VOID dd_close();
#endif

/* COPIED from t1imap.c */
#if PST1_RDR

#ifdef LINT_ARGS	/* 08-16-01 jfd */
EXTERN PVOID  IndexToCgid(UW16);
EXTERN UW16   cm_cgidToIndex(UW16);
EXTERN UW16   psnameToIndex(LPSB8);
#else
EXTERN PVOID  IndexToCgid();
EXTERN UW16   cm_cgidToIndex();
EXTERN UW16   psnameToIndex();
#endif

#if CFF_RDR
#ifdef LINT_ARGS
EXTERN UW16   StdStrToIndex(LPSB8);
#else
EXTERN UW16   StdStrToIndex();
#endif
#endif	/* CFF_RDR */

#endif	/* PST1_RDR */


/**** NEW SECTION end ****/


/* Moved from bitmap.h */
#ifdef LINT_ARGS
EXTERN UW16       make_gaso(FSP PBUCKET, SL32, PCHR_DEF);
EXTERN BOX        char_left_ref(FSP SW16VECTOR, BOX, SW16);
EXTERN SW16VECTOR find_part2whole(FSP SW16VECTOR, SW16VECTOR, PCHR_DEF);
EXTERN VOID       union_bound_boxes(PBOX, PBOX, SW16, SW16);
EXTERN VOID       cgfill(FSP LPUB8, LPUB8, SL32, SL32, LPUB8);
EXTERN VOID       metrics(FSP PIFBITMAP);
EXTERN VOID       merge(FSP PIFBITMAP, PIFBITMAP, SW16VECTOR);

#ifdef AGFADEBUG
EXTERN VOID       print_bm(FSP PIFBITMAP);
#endif

#else /* no LINT_ARGS */
EXTERN UW16       make_gaso();
EXTERN BOX        char_left_ref();
EXTERN SW16VECTOR find_part2whole();
EXTERN VOID       union_bound_boxes();
EXTERN VOID       cgfill();
EXTERN VOID       metrics();
EXTERN VOID       merge();
#ifdef AGFADEBUG
EXTERN VOID       print_bm();
#endif
#endif /* LINT_ARGS */
/* end of prototypes moved from bitmap.h */


/* Moved from nzwind.h */
#ifndef WINDCOMP
#if NON_Z_WIND
#ifdef LINT_ARGS
EXTERN VOID nz_init ( FSP PNZ_INSTANCE, LPUB8, NZCOUNTER, SW16 );
EXTERN VOID nz_open_run ( FSP PNZ_INSTANCE, NZCOUNTER, SL32 );
EXTERN VOID nz_close_run ( FSP PNZ_INSTANCE,  SL32, BOOLEAN );
EXTERN UW16 nz_set_trans( FSP PNZ_INSTANCE );
EXTERN UW16 checkContours( FSP PNZ_INSTANCE nz );
#else
EXTERN VOID nz_init();
EXTERN VOID nz_open_run();
EXTERN VOID nz_close_run();
EXTERN UW16 nz_set_trans();
EXTERN UW16 checkContours();
#endif
#endif  /* NON_Z_WIND */
#endif /* WINDCOMP */
/* end of prototypes moved from nzwind.h */


/* moved from ifmem.h */
#ifdef LINT_ARGS
EXTERN VOID       MEMinit(FSP0);
EXTERN MEM_HANDLE MEMalloc(FSP UW16, SL32);
EXTERN VOID       MEMfree(FSP UW16, MEM_HANDLE);
#else
EXTERN VOID       MEMinit();
EXTERN MEM_HANDLE MEMalloc();
EXTERN VOID       MEMfree();
#endif /* LINT_ARGS */

#if INT_MEM_MGT   /*  If CG memory manager  */

#ifdef LINT_ARGS
#if (HUGE_PTR_SUPPORT)
UFST_EXTERNAL_INTERFACE HPSB8      hMEMptr(FSP MEM_HANDLE);
#define MEMptr(h)    (LPSB8)hMEMptr(FSA h)
#else
UFST_EXTERNAL_INTERFACE LPSB8     MEMptrFSP(FSP MEM_HANDLE);
#define MEMptr(h)    (LPSB8)MEMptrFSP(FSA h)
#endif

#else        /* !LINT_ARGS */
#if (HUGE_PTR_SUPPORT)
UFST_EXTERNAL_INTERFACE HPSB8      hMEMptr();
#else
UFST_EXTERNAL_INTERFACE LPSB8      MEMptr();
#endif
#endif       /* !LINT_ARGS */

#endif	/* INT_MEM_MGT */
/* end of prototypes moved from ifmem.h */


/* moved from fpmath.h */

/*  Function definitions shared between internal / external FP  */
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

#if INT_FP

#define fpiszero(x)   (!x.n)             /* TRUE if x is 0         */
#define fpisneg(x)    (x.n<0)            /* TRUE if x < 0          */
#define fpint2fp(x)   fplongexp(x, (SW16)0)    /* Convert SL32 to FPNUM  */
#define fpfixed2fp(x) fplongexp(x, (SW16)16)   /*         FIXED to FPNUM */

        /*  Function definitions  */

#ifdef LINT_ARGS
EXTERN FPNUM   fpatofFSP (FSP LPSB8);         /*         ASCII to FPNUM */
EXTERN FPNUM   fpneg(FPNUM);          /* Negate FPNUMs          */
EXTERN FPNUM   fpaddFSP (FSP FPNUM, FPNUM);   /* Add                    */
EXTERN FPNUM   fpsubFSP (FSP FPNUM, FPNUM);   /* Subtract               */
EXTERN FPNUM   fpmulFSP (FSP FPNUM, FPNUM);   /* Multiply               */
EXTERN BOOLEAN fplt(FPNUM, FPNUM);    /* Compare Less Than      */
EXTERN FPNUM   fpabs(FPNUM);          /* Absolute value         */
EXTERN SW16    ifpmulFSP (FSP SW16, FPNUM);   /* Multiply SW16 * FPNUM  */
EXTERN FPNUM   fpsqrtFSP (FSP FPNUM);         /* sqrt function          */
#else     /* !LINT_ARGS */
EXTERN FPNUM   fpatofFSP();
EXTERN FPNUM   fpneg();
EXTERN FPNUM   fpaddFSP();
EXTERN FPNUM   fpsubFSP();
EXTERN FPNUM   fpmulFSP();
EXTERN BOOLEAN fplt();
EXTERN FPNUM   fpabs();
EXTERN SW16    ifpmulFSP();
EXTERN FPNUM   fpsqrtFSP();
#endif	/* LINT_ARGS */

#define fpatof(str) fpatofFSP (FSA str)
#define fpadd(n1, n2) fpaddFSP(FSA n1, n2)
#define	fpsub(n1, n2) fpsubFSP(FSA n1, n2)
#define fpmul(n1, n2) fpmulFSP (FSA n1, n2)
#define ifpmul(n1, n2) ifpmulFSP (FSA n1, n2)
#define fpsqrt(num) fpsqrtFSP (FSA num)

#else   /* !INT_FP */

/*  Define an FPNUM to be a double in port.h  */

        /*  Macro definitions  */

#define fpiszero(x)   ((x) == 0.0)              /* TRUE if x is 0         */
#define fpisneg(x)    ((x) < 0.0)               /* TRUE if x < 0.0        */
#define fpint2fp(x)   ((FPNUM)(x))              /* Convert SL32 to FPNUM  */
#define fpfixed2fp(x) ((FPNUM)(x)/65536.0)      /*         FIXED to FPNUM */
#define fpatof(x)     (atof(x))                 /*         ASCII to FPNUM */
#define fpneg(x)      (-(x))                    /* Negate FPNUMs          */
#define fpadd(x, y)   ((x)+(y))                 /* Add                    */
#define fpsub(x, y)   ((x)-(y))                 /* Subtract               */
#define fpmul(x, y)   ((x)*(y))                 /* Multiply               */
#define fplt(x, y)    ((x)<(y))                 /* Compare Less Than      */
#define fpabs(x)      (((x)<0.0) ? (-(x)):(x))  /* Absolute value         */
#define ifpmul(x, y)  fp2word((FPNUM)x*y)       /* Multiply SW16 * FPNUM  */
#define fpsqrt(x)     sqrt(x)                   /* sqrt(FPNUM) [math.h]   */


#endif /* INT_FP */
/* end of prototypes moved from fpmath.h */


/* moved from cache.h */
#ifdef LINT_ARGS
EXTERN MEM_HANDLE  CHARalloc(FSP SL32);
UFST_EXTERNAL_INTERFACE VOID        CHARfree(FSP MEM_HANDLE);

EXTERN MEM_HANDLE  TEMPCHARalloc(FSP SL32);
UFST_EXTERNAL_INTERFACE VOID        TEMPCHARfree(FSP MEM_HANDLE);

#else   /*  not LINT_ARGS  */
EXTERN MEM_HANDLE  CHARalloc();
UFST_EXTERNAL_INTERFACE VOID        CHARfree();

EXTERN MEM_HANDLE  TEMPCHARalloc();
UFST_EXTERNAL_INTERFACE VOID        TEMPCHARfree();
#endif  /* LINTARGS */


#if CACHE
#ifdef LINT_ARGS
EXTERN UW16       CACinit(FSP0);
EXTERN VOID        CACexit(FSP0);
EXTERN UW16       CACfont(FSP PFONTCONTEXT);

EXTERN HIFBITMAP  CACinsert(FSP HIFBITMAP, UL32);   /* rjl 6/5/2002 - param 2 was UW16 */
EXTERN UW16       CACpurge(FSP PFONTCONTEXT);
#if DEFUND
EXTERN BOOLEAN     CACdefund(FSP UW16);
#endif

EXTERN MEM_HANDLE CACbmLookup(FSP PFONT, UL32); /* rjl 6/5/2002 - param 2 was UW16*/
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

#endif  /* LINTARGS */

#endif /* CACHE */
/* end of prototypes moved from cache.h */


/* moved from sym.h */
#ifdef LINT_ARGS
EXTERN UW16    SYMmap(FSP SW16, SL32);
EXTERN UW16    SYMinit(FSP0);
EXTERN VOID    SYMexit(FSP0);
EXTERN BOOLEAN SYMnew(FSP UW16, UW16);
EXTERN BOOLEAN SYMnoMap(FSP LPUB8, UW16);
#else
EXTERN UW16    SYMmap();
EXTERN UW16    SYMinit();
EXTERN VOID    SYMexit();
EXTERN BOOLEAN SYMnew();
EXTERN BOOLEAN SYMnoMap();
#endif
/* end of prototypes moved from sym.h */



#if FCO_RDR
/* moved from fc_dafil.h */
#if defined (LINT_ARGS)
DAFILE* DAopen   (FSP LPUB8 fcName, DAFILE *f);
DAFILE* DAclose  (FSP DAFILE* f);
DAFILE* DAsetPos (FSP DAFILE* f, SL32 offset);
SW16   DAgetSWord(FSP DAFILE *f);
SL32    DAgetLong(FSP DAFILE *f);
/* Extract 4 bytes from file. Rarely used, so this is function */
SL32    DAgetSL32(FSP DAFILE*);
#if FCO_DISK
UINTG   DAseek_rd(FSP DAFILE*, SL32, UINTG, LPUB8);
void FC_RelMemPtr (FSP MEM_HANDLE h);
#endif
#else
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
#endif
/* end of prototypes moved from fc_dafil.h */
/* moved from fc_da.h */
#if defined (LINT_ARGS)
UW16      FCnew      ( FSP FCTYPE*, UB8* );
UW16      FCclose    ( FSP FCTYPE* );
SL32       FCnumVersion(FCTYPE* fc);
SL32       FCnumFonts (FCTYPE* fc);
UB8* FCgetStringsPtr (FSP FCTYPE* fc);
UB8* FCgetPostStringsPtr (FSP FCTYPE* fc);
UB8* FCgetYClassPtr  (FSP FCTYPE* fc);
UW16      FCaccessNew      ( FSP FCACCESSTYPE*, UB8*, INTG );

#else  /* !LINT_ARGS */
UW16      FCnew      ();
UW16      FCclose    ();
SL32       FCnumVersion();
SL32       FCnumFonts ();
UB8* FCgetStringsPtr();
UB8* FCgetPostStringsPtr();
UB8* FCgetYClassPtr ();
UW16     FCaccessNew();
#endif

#if defined (LINT_ARGS)
UW16       FCfontNew  ( FSP FONTTYPE*, FCTYPE*, INTG, INTG );
UW16       FCfontClose( FSP FONTTYPE* );
UW16       FCcharNew  ( FSP CHARTYPE*, FONTTYPE*, UINTG, INTG );
UW16       FCcharClose( FSP CHARTYPE* );
UW16       FCcompCharPiece( FSP COMPPIECETYPE*, CHARTYPE*, INTG );
UW16       FCcompCharShort( FSP CHARTYPE*, COMPPIECETYPE*, SL32*, SL32*,
                            INTG*, INTG* );
#if PLUGINS
UW16       FCpluginNew( FSP FCTYPE*, TREETYPE* );
#endif
#if FONTBBOX || FNT_METRICS
UW16       FCfontInfoIndex( FSP TTFONTINFOTYPE*, FCTYPE*, INTG );
#endif
SL32        GETBIT( UB8**, SL32* );
SL32        GETDIBBLE( UB8**, SL32* );
SL32        GETNIBBLE( UB8**, SL32* );
SL32        GETBYTE( UB8**, SL32* );

#else   /* !LINT_ARGS */
UW16       FCfontNew();
UW16       FCfontClose();
UW16       FCcharNew();
UW16       FCcharClose();
UW16       FCcompCharPiece();
UW16       FCcompCharShort();
#if PLUGINS
UW16       FCpluginNew();
#endif
#if FONTBBOX || FNT_METRICS
UW16       FCfontInfoIndex();
#endif
SL32        GETBIT();
SL32        GETDIBBLE();
SL32        GETNIBBLE();
SL32        GETBYTE();

#endif  /* !LINT_ARGS */
/* end of prototypes moved from fc_da.h */
/* moved from fc_intfl.h */
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

#else /*!LINT_ARGS*/
UL32 intelliflator();
void         in_VectorScaleGrid();
#endif /*LINT_ARGS*/
/* end of prototypes moved from fc_intfl.h */
#endif	/* FCO_RDR */

#if TT_RDR && TT_PCLEOI
/* moved from ttpcleo.h */
#ifdef LINT_ARGS
EXTERN SL32  ttload_PCLEO (FSP LPUB8, PBUCKET);
#else /* !LINT_ARGS */
EXTERN SL32  ttload_PCLEO ();
#endif /* LINT_ARGS */
/* end of prototypes moved from ttpcleo.h */
#endif	/* TT_RDR && TT_PCLEOI */


/* moved from tt_if.c */
#if (TT_ROM)
#ifdef LINT_ARGS
GLOBAL SL32  ttload_ROMSfnt( FSP PBUCKET );
#else
GLOBAL SL32  ttload_ROMSfnt();
#endif
#endif

/* moved from kanji.h */
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

#if ((UNICODE_IN || (UNICODE_MAPPING & JIS2UNI_MAP) \
                 || (UNICODE_MAPPING & KSC2UNI_MAP) \
                 || (UNICODE_MAPPING & BIG52UNI_MAP)\
                 || (UNICODE_MAPPING & GB2UNI_MAP) ) && DISK_FONTS)
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

#if ((UNICODE_IN || (UNICODE_MAPPING & JIS2UNI_MAP) \
                 || (UNICODE_MAPPING & KSC2UNI_MAP) \
                 || (UNICODE_MAPPING & BIG52UNI_MAP)\
                 || (UNICODE_MAPPING & GB2UNI_MAP) ) && DISK_FONTS)
EXTERN VOID ASIANexit();
#endif
#endif /* LINT_ARGS */
#endif	/* ASIAN_ENCODING */
/* end of prototypes moved from kanji.h */

#if COMPRESSED_CACHE 
/* moved from cmpr.h */
#ifdef LINT_ARGS
EXTERN UW16 compress_ifbitmap(FSP PIFBITMAP src, PIFBITMAP buf, PHIFBITMAP phifbm);
EXTERN UW16 decompress_ifbitmap(FSP PIFBITMAP src, PIFBITMAP dst);
#else
EXTERN UW16 compress_ifbitmap();
EXTERN UW16 decompress_ifbitmap();
#endif
/* end of prototypes moved from cmpr.h */
#endif	/* COMPRESSED_CACHE */

#if IF_RDR
/* moved from if_type.h */
#ifdef LINT_ARGS
EXTERN VOID       first_loop (FSP0);
EXTERN VOID       next_loop (FSP PLOOP);
EXTERN VOID       pixel_align (FSPvoid SW16, PCOORD_DATA, UW16,PALIGNED);
EXTERN VOID       sd_pixel_align (FSPvoid SW16, PCOORD_DATA, UW16,PALIGNED);
EXTERN UW16       intel_char(FSP PBUCKET);
EXTERN SW16VECTOR  des2bm(FSP SW16VECTOR);
EXTERN SW16VECTOR  inv_des2bm(FSP SW16VECTOR);
#else 
EXTERN VOID       first_loop ();
EXTERN VOID       next_loop ();
EXTERN VOID       pixel_align ();
EXTERN VOID       sd_pixel_align ();
EXTERN UW16   intel_char();
EXTERN SW16VECTOR  des2bm();
EXTERN SW16VECTOR  inv_des2bm();
#endif /* LINT_ARGS */
/* end of prototypes moved from if_type.h */

/* moved from cgif.h */
#if !defined(_AM29K)
#define MDES2BM(v) \
    ( (!if_state.quadrant) /* arbitrary rotation */  \
      ?  des2bm(FSA v)                                   \
      :  cg_scale(FSA v) )
#endif

#ifdef LINT_ARGS
EXTERN LPSB8	fgseg(FSP PBUCKET);
#else
EXTERN LPSB8    fgseg();
#endif /* LINT_ARGS */
#endif	/* IF_RDR */


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
#endif  /* !ANSI_DEFS */
#endif	/* FCO_STANDALONE */


/* centralized - to match raster.c declaration */
#if ((TT_RDR || FCO_RDR) && (CGBITMAP || LINEAR || SMEAR_BOLD || GRAYSCALING))
#ifdef LINT_ARGS
EXTERN UW16   quadvectorize(FSP PVOID s, UW16 (*lineto)(FSPvoid PVOID, INTR, INTR),
                     INTR p0x, INTR p0y, INTR cx, INTR cy, INTR px, INTR py);
#else
EXTERN UW16   quadvectorize();
#endif  /* LINT_ARGS */
#endif	/* (TT_RDR || FCO_RDR) && (CGBITMAP || LINEAR || SMEAR_BOLD || GRAYSCALING) */

#if ((PST1_RDR) && (CGBITMAP || LINEAR || SMEAR_BOLD || GRAYSCALING))
#ifdef LINT_ARGS
EXTERN UW16  cubevectorize(FSP PVOID s, UW16 (*lineto)(FSPvoid PVOID, INTR, INTR),
                     INTR p0x, INTR p0y, INTR p1x, INTR p1y,
                     INTR p2x, INTR p2y, INTR p3x, INTR p3y);
#else
EXTERN UW16   cubevectorize();
#endif  /* LINT_ARGS */
#endif	/* (PST1_RDR) && (CGBITMAP || LINEAR || SMEAR_BOLD || GRAYSCALING) */
/* end raster.c declarations */

#if EMBEDDED_BITMAPS && TT_RDR && CGBITMAP
#ifdef LINT_ARGS
EXTERN UW16		embedded_bitmap(FSP PIFBITMAP, PHIFBITMAP);
EXTERN UW16		embedded_character(FSP PEMBEDDED_BITMAP_FONT, PEMBEDDED_BITMAP, PIFBITMAP);
EXTERN VOID		get_embedded_bitmap_metrics(FSP PEMBEDDED_BITMAP_FONT, PEMBEDDED_BITMAP, PFONTCONTEXT, PIFBITMAP);
EXTERN BOOLEAN	embedded_bitmap_exists(FSP PEMBEDDED_BITMAP_FONT, PEMBEDDED_BITMAP, /* SW16 */ UW16);
#else
EXTERN UW16		embedded_bitmap();
EXTERN UW16		embedded_character();
EXTERN VOID		get_embedded_bitmap_metrics();
EXTERN BOOLEAN	embedded_bitmap_exists();
#endif
#endif	/* EMBEDDED_BITMAPS && TT_RDR && CGBITMAP */

#if TT_RDR
#ifdef LINT_ARGS
EXTERN VOID*  tt_GetFragment (FSP PBUCKET pBucket, SL32 offset, SL32 length);
#else 
EXTERN VOID*  tt_GetFragment ();
#endif
#endif

#if STIK && (TT_DISK || TT_ROM || TT_ROM_ACT)
#ifdef LINT_ARGS
EXTERN FS_OUTLINE * extract_outline(FSP fsg_SplineKey *);
EXTERN UB8 get_nstk_bit(FSP UW16);
EXTERN FS_OUTLINE * expand_stik(FSP fsg_SplineKey *, FS_OUTLINE *);
EXTERN UW16 do_auto_hint(FSP fsg_SplineKey *, FS_OUTLINE *);
EXTERN VOID    extract_reflines(FSP PFONTCONTEXT);
#else
EXTERN FS_OUTLINE * extract_outline();
EXTERN UB8 get_nstk_bit();
EXTERN FS_OUTLINE * expand_stik();
EXTERN UW16 do_auto_hint();
EXTERN VOID    extract_reflines();
#endif
#endif	/* STIK && (TT_DISK || TT_ROM || TT_ROM_ACT) */

/* prototypes for various debugging routines */
#ifdef LINT_ARGS

/* access functions for if_state.trace_sw debug switch */
VOID UFST_debug_on(FSP0);
VOID UFST_debug_off(FSP0);
SW16 UFST_get_debug(FSP0);

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


#endif	/* __SHAREINC__ */
