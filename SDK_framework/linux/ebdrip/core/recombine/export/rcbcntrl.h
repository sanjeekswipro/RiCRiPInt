/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!export:rcbcntrl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine control API
 */

#ifndef __RCBCNTRL_H__
#define __RCBCNTRL_H__

#include "graphict.h"     /* GS_COLORinfo, GUCR_RASTERSTYLE */
#include "gs_color.h"     /* COLORSPACE_ID */
#include "gscsmpxform.h"  /* EQUIVCOLOR */

struct NAMECACHE ; /* from COREobjects */
struct Group;

enum {
  RCBN_CONFIDENCE_NONE,
  RCBN_CONFIDENCE_LO,
  RCBN_CONFIDENCE_MED1,
  RCBN_CONFIDENCE_MED2,
  RCBN_CONFIDENCE_MED3,
  RCBN_CONFIDENCE_HI,
  RCBN_CONFIDENCE_MAX
} ;

typedef struct RCBSEPARATION RCBSEPARATION ;


Bool rcbn_start( void ) ;
Bool rcbn_reset( void ) ;
Bool rcbn_beginpage(DL_STATE *page);
Bool rcbn_endpage(DL_STATE *page, struct Group *pageGroup,
                  Bool recombined, Bool savedOverprintBlack);
void rcbn_term( void ) ;
void rcbn_quit( void ) ;

Bool rcbn_enabled( void ) ;
Bool rcbn_intercepting( void ) ;

void rcbn_enable_interception(GS_COLORinfo *colorInfo) ;
void rcbn_disable_interception(GS_COLORinfo *colorInfo) ;

Bool rcbn_register_separation(struct NAMECACHE *sepname, int32 nconfidence) ;
Bool rcbn_register_showpage( int32 ncopies ) ;
Bool rcbn_register_deactivate(Bool forced, Bool fautopage, int32 ncopies) ;

Bool rcbn_do_beginpage( void ) ;
Bool rcbn_do_endpage( void ) ;
Bool rcbn_do_render( void ) ;
Bool rcbn_do_deactivate( void ) ;

COLORANTINDEX rcbn_current_colorant( void );
void rcbn_current_equiv_details(int32 **level, EQUIVCOLOR **equivs);

/* Routines to build or enquire about screens */
Bool rcbn_build_preseparated_screens( void );
COLORANTINDEX rcbn_presep_screen(struct NAMECACHE **nmPseudo);
COLORANTINDEX rcbn_likely_separation_colorant( void );
Bool rcbn_use_default_screen_angle( COLORANTINDEX ci );

COLORANTINDEX rcbn_aborted_colorant(void);

Bool rcbn_composite_page( void ) ;
Bool rcbn_first_separation( void ) ;
Bool rcbn_merge_required(int32 opcode) ;
void rcbn_object_merge_result(uint8 opcode, int32 merged);
Bool rcbn_order_important( void ) ;

void rcbn_copies( int32 *ncopies ) ;

void rcbn_add_recombine_object( void ) ;
void rcbn_set_recombine_object( int32 cn ) ;

COLORSPACE_ID rcbn_icolorspace( void ) ;
int32 rcbn_ncolorspace( void ) ;
struct NAMECACHE **rcbn_nmcolorspace( void ) ;

int32 rcbn_cseps( void ) ;
int32 rcbn_cspotseps( void ) ;
int32 rcbn_cprocessseps( void ) ;

COLORANTINDEX rcbn_nm_ciPseudo(struct NAMECACHE *nmActual) ;
COLORANTINDEX rcbn_ciPseudo( COLORANTINDEX ciActual ) ;
COLORANTINDEX rcbn_ciActual( COLORANTINDEX ciPseudo ) ;
struct NAMECACHE *rcbn_nmActual( COLORANTINDEX ciPseudo ) ;
struct NAMECACHE *rcbn_nmActual_and_equiv(GUCR_RASTERSTYLE *rasterStyle,
                                          COLORANTINDEX ciActual ,
                                          EQUIVCOLOR **equiv,
                                          void *private_data ) ;

Bool rcbn_is_pseudo_separation(const struct NAMECACHE *sepname) ;

RCBSEPARATION *rcbn_iterate( RCBSEPARATION *prev ) ;

Bool rcbn_sepisprocess( RCBSEPARATION *rcbs ) ;
COLORANTINDEX rcbn_sepciPseudo( RCBSEPARATION *rcbs ) ;
struct NAMECACHE *rcbn_sepnmActual(RCBSEPARATION *rcbs) ;

/*****************************************************************************/
Bool rcb_dl_start(DL_STATE *page);
Bool rcb_dl_finish(DL_STATE *page);
Bool rcb_resync_dlptrs(DL_STATE *page);

/*****************************************************************************/
/* Define the test types for the COMPARE_LOBJS functions. These bits are
 * ORed together to build distinct tests. Currently only characters support
 * a fuzzy test caused by Quark's style of overprinting text */
enum {
  /* Merge result */
  MERGE_NONE = 0x00,

  /* Merge type */
  MERGE_EXACT = 0x01,
  MERGE_FUZZY = 0x02,

  /* Merge operation */
  MERGE_SPLIT = 0x04,
  MERGE_CHECK = 0x08,

  /* Merge status */
  MERGE_ERROR = 0x10,
  MERGE_DONE =  0x20
} ;

/** Bitmask of MERGE_* flags. */
typedef unsigned int rcb_merge_t ;

enum {
  RCBV_ELEMENTS_MATCH = 0 ,
  RCBV_OUTLINE_MATCH  = 1
} ;

typedef rcb_merge_t (*COMPARE_LOBJS)(LISTOBJECT *old_lobj,
                                     LISTOBJECT *new_lobj,
                                     rcb_merge_t test_type);

extern COMPARE_LOBJS rcb_comparefn(int32 opcode);
extern unsigned int rcb_compareop(int32 opcode);

rcb_merge_t compare_vignette_splitter(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                      rcb_merge_t test_type);

rcb_merge_t merge_dl_objects(DL_STATE *page, LISTOBJECT *new_lobj,
                             COMPARE_LOBJS compare_lobjs,
                             rcb_merge_t test_type);

#endif /* protection for multiple inclusion */


/* Log stripped */
