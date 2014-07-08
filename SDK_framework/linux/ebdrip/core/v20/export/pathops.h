/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!export:pathops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Defines and API for PS path construction operators
 */

#ifndef __PATHOPS_H__
#define __PATHOPS_H__

#include "graphics.h"
#include "implicitgroup.h"

/**
 * Public interface parts, for communicating information to the
 * stroke algorithm.
 * Note that the stroke algorithm is at liberty to modify these.
 */
typedef struct STROKE_PARAMS {
  DL_STATE *page;
  OMATRIX orig_ctm;        /* CTM for strokes */
  PATHINFO *thepath;       /* Input path */
  PATHINFO *strokedpath;   /* Output path */
  LINESTYLE linestyle;     /* width, dashing, caps etc. */
  uint8 strokeadjust;      /* adjust points before stroking? */
  uint8 usematrix;         /* adjust CTM by matrix before use? */
  uint8 copypath;          /* copy path for use in charpaths? */
  uint8 spare;

  /**
   * Note : This structure used to be a combination of both the
   * public API and private workspace parts.
   * The new stroker algorithm has pulled all the private workspace
   * bits local, so they really are private !
   * But there are a few legacy private bits left behind in this structure
   * which need to be cleaned out.
   *
   * \todo BMJ 23-May-08 :  Make private stroke stuff really private !
   *
   * Private workspace parts for the stroke algorithm.
   */
  OMATRIX orig_inv;        /* Inverted CTM for strokes. */
  OMATRIX sadj_ctm;        /* stroke-adjusted version of CTM */
  OMATRIX sadj_inv;        /* Inverse of the adjusted matrix */
  PATHINFO outputpath;     /* Building up the output path */
  USERVALUE lby2;          /* Line width divided by two. */
  SYSTEMVALUE dashcurrent; /* current dash (offset) */

  /* Rounding variables. */
  SYSTEMVALUE sc_rndx1, sc_rndy1;     /* First rounding. */
  SYSTEMVALUE sc_rndx2, sc_rndy2;     /* Second rounding. */
} STROKE_PARAMS;

/* Flags for dostroke() */
typedef enum {
  STROKE_NORMAL = 0,       /* Normal stroke, with all knobs & whistles. */
  STROKE_NOT_VIGNETTE = 1, /* Don't do vignette detection */
  STROKE_NO_SETG = 2,      /* Don't do DEVICE_SETG() */
  STROKE_NO_HDLT = 4,      /* Don't do HDLT */
  STROKE_NO_PDFOUT = 8,    /* Don't do PDF output */
  STROKE_IS_TRAP = 16      /* Stroke is a trap. For future use. */
} STROKE_OPTIONS ;

 /** Possible values for the ForceStrokeAdjust SystemParam */
enum {
  NO_FORCE = 0,                /** honour the gstate devicestrokeadjust value */
  FORCE_TRUE = 1,              /** stroke adjust regardless of the gstate value */
  FORCE_FALSE = 2,             /** do not stroke adjust regardless of the gstate value */
  COMPATIBILITY_TRUE = 3,      /** old style stroke adjust selected by /ForceStrokeAdjust true */
  COMPATIBILITY_FALSE = 4      /** = NO_FORCE, provided so currentsystemparams returns false */
};

Bool dostroke(STROKE_PARAMS *params, int32 colorType, STROKE_OPTIONS options) ;
Bool dostroke_draw(STROKE_PARAMS *params) ;
extern void  set_gstate_stroke( STROKE_PARAMS *params , PATHINFO *input ,
                                PATHINFO *output , uint8 copycharpath ) ;
extern void  set_font_stroke( DL_STATE *page, STROKE_PARAMS *params , PATHINFO *input ,
                              PATHINFO *output ) ;
Bool add_charpath(PATHINFO *path, Bool copycharpath) ;

Bool dostrokefill(PATHINFO *inpath, PATHINFO *inpathfill, Bool use_inpathfill,
                  USERVALUE stroke_opacity, USERVALUE fill_opacity,
                  IMPLICIT_GROUP_USAGE usage, Bool closepath, int32 filltype) ;

Bool set_bbox( SYSTEMVALUE args[ 4 ] , PATHINFO *path ) ;
void bbox_transform(const sbbox_t *ibbox, sbbox_t *obbox, OMATRIX *m) ;

Bool ForceStrokeAdjustApply( Bool compatibility );
Bool set_ForceStrokeAdjust( struct SYSTEMPARAMS *systemparams, OBJECT *theo );
void get_ForceStrokeAdjust( struct SYSTEMPARAMS *systemparams, OBJECT *result );

#if defined(DEBUG_BUILD)
enum
{
  DEBUG_STROKE_DUMP_PATH   = 1,  /* dump output stroked path */
};

extern int32 debug_stroke ;

void init_stroke_debug(void);

#endif /* DEBUG_BUILD */

#endif /* protection for multiple inclusion */

/* Log stripped */
