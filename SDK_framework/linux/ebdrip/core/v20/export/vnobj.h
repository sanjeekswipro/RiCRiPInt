/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:vnobj.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Vignette detection full type definitions
 */

#ifndef __VNOBJ_H__
#define __VNOBJ_H__

#include  "vntypes.h"
#include  "dl_color.h"
#include  "displayt.h"


struct rcbv_compare_t ; /* from CORErecombine */

/* ----- External structures ----- */

typedef struct vn_outlines_t {
  NFILLOBJECT *nfillb; /**< basic outline */
  NFILLOBJECT *nfillr; /**< rolled outline: Quark precedes vignette with rect */
  NFILLOBJECT *nfills; /**< special outline for when applications screw up */
  NFILLOBJECT *nfillh; /**< overprint outline when head of vignette missing */
  NFILLOBJECT *nfillt; /**< overprint outline when tail of vignette missing */
  NFILLOBJECT *nfillo; /**< outline to be used for trapping */
  NFILLOBJECT *nfillm; /**< outline to be used for matching */
  uint8 freenfillb;    /**< does basic outline need freeing? */
  uint8 freenfillr;    /**< does special outline need freeing? */
  uint8 freenfillh;    /**< does extended at head outline need freeing? */
  uint8 freenfillt;    /**< does extended at tail outline need freeing? */

  LISTOBJECT *outline_lobj;    /**< outline object of vignette;
                                    thickish unclipped red stroke */
} vn_outlines_t ;


/**
 * Information about extra white DL objects that may be inserted at the head
 * and tail of the Vignette DL.
 */
typedef struct vig_whiteinfo_t {
  Bool        dropped;
  Bool        used;
  LISTOBJECT *lobj;
  uint8       h;     /**< May have been overprinted white objs before head */
  uint8       t;     /**< May have been overprinted white objs after tail */
} vig_whiteinfo_t ;


/** Color attributes. */
enum { VDC_Unknown, VDC_Neutral, VDC_Increasing, VDC_Decreasing } ;

/** Confidence attributes. */
enum { VDC_Low = 0 , VDC_High = 1 } ;

/** Style attributes. */
enum { VDS_Unknown, VDS_StrongContained, VDS_Contained, VDS_Adjacent,
       VDS_Overlapped, VDS_Spaced };

/**
 * Top level DL data structure in the Display list representing a vignette
 */
struct VIGNETTEOBJECT {
  int32 vcount;            /**< Number of chained LISTOBJECTs in use */
  HDL   *vhdl;             /**< hdl of the objects in the vignette */
  vn_outlines_t outlines;  /**< Container for various kinds of outlines */
  struct rcbv_compare_t *compareinfo; /**< Stores info from compare
                                           to be used in a subsequent merge */
  p_ncolor_t partialcolors;/**< When sub-part of vignette has been merged. */
                           /**< Extension object of vignette
                                if next step white */
  uint8 style;             /**< Style attribute of vignette. */
  uint8 confidence;        /**< Our confidence level if this is a vignette. */
  uint8 rolledrect;        /**< Is vignette preceded by a (Quark) rectangle. */
  uint8 colormonotonic;    /**< Direction of color movement (recombine). */
  uint8 recurse;           /**< Use full render loop to handle clip change. */
  uint8 rollover;          /**< Use self-intersecting (rollover) blit. */
  vig_whiteinfo_t white;   /**< Information about extra white objects */
};

#if defined( DEBUG_BUILD )
LISTOBJECT *debug_vignette_red_outline(LISTOBJECT *lobj);
#endif

Bool vn_alloc_vignette(DL_STATE *page, LISTOBJECT *lobj, int32 count);
Bool vn_complete_vignette(void);

void vn_merge_vignetteobject( LISTOBJECT *lobj_merge ,
                              LISTOBJECT *lobj_old ,
                              LISTOBJECT *lobj_new);

DLREF *vig_dlhead(LISTOBJECT *lobj);
int32 vig_len(VIGNETTEOBJECT *vigobj);
LISTOBJECT *vn_white_insert_vignette(DL_STATE *page,
                                     LISTOBJECT *vig_lobj, Bool atHead,
                                     LISTOBJECT *white_lobj);

#endif /* protection for multiple inclusion */


/* Log stripped */
