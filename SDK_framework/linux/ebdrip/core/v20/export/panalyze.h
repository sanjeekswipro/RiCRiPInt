/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:panalyze.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS path analysis API
 */

#ifndef __PANALYZE_H__
#define __PANALYZE_H__


/* panalyze.h */

struct RCBTRAP ;

/* ----- External constants ----- */

/* path  accuracy */
#define PA_EPSILON       (0.000001) /* One millionth of a pixel */
#define PA_EPSILON_LARGE     (0.06) /* For large errors after Distilling */

/* Orientation attributes. */
enum { VDO_Unknown, VDO_ClockWise, VDO_AntiClockWise } ;

/* Type attributes. */
enum {
  /* Type not determined. */
  VDT_Unknown,

  /* Used for vignette detection. */
  VDT_Line,
  VDT_Stroke,
  VDT_Circle,
  VDT_ReverseCircle,
  VDT_RectangleUser,
  VDT_RectangleDevice,

  /* Used for (NYI) complex path vignette detection. */
  VDT_Simple,
  VDT_Complex,

  /* Following used for Quark traps (with recombine). */
  VDT_Octagon,
  VDT_RoundRectangle,
  VDT_RevRoundRectangle,

  /* Used when determining VDT_RoundRectangle (for recombine). */
  VDT_RoundRectangleLineCurve,
  VDT_RoundRectangleCurveLine
} ;

/* Match attributes. */
enum {
  VDM_Unknown =    0x0,
  VDM_Exact =      0x1,
  VDM_Translated = 0x2,
  VDM_Scaled =     0x4,
  VDM_Rotated =    0x8,
  VDM_All =        VDM_Exact | VDM_Translated | VDM_Scaled | VDM_Rotated
} ;

/* Rolled up match attributes. */
enum {
  VDR_Unknown,
  VDR_Diamond,
  VDR_FullDiamond,
  VDR_Circular,
  VDR_FullCircular,
  VDR_MidLinear
} ;

/* ----- External structures ----- */

/* ----- External global variables ----- */

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

void init_path_analysis( void ) ;

int32 getpathrectanglepoints( PATHINFO *path , FPOINT *points ) ;
int32 getpathcirclepoints( PATHINFO *path , FPOINT *points ) ;
void get_path_matrix( PATHINFO *path , OMATRIX *matrix , SYSTEMVALUE *length ) ;

Bool pathiscorneredrect(PATHINFO *path ,
                        Bool *degenerate, int32 *orientation, int32 *type,
                        struct RCBTRAP *rcbtrap) ;
Bool pathisarectangle(PATHINFO *path ,
                      Bool *degenerate, int32 *orientation, int32 *type,
                      struct RCBTRAP *rcbtrap) ;
Bool pathisacircle(PATHINFO *path ,
                   Bool *degenerate, int32 *orientation, int32 *type,
                   struct RCBTRAP *rcbtrap) ;
Bool pathisaline(PATHINFO *path, Bool *degenerate, Bool *closed) ;

Bool pathsaresimilar(PATHINFO *path1 , PATHINFO *path2,
                     Bool fcircle , int32 tests , int32 hint ,
                     int32 *match) ;
Bool pathsareadjacent(PATHINFO *path, SYSTEMVALUE dx, SYSTEMVALUE dy) ;

Bool strokedpathsaresimilar(PATHINFO *path1, PATHINFO *path2,
                            Bool fcircle, int32 tests, int32 hint,
                            int32 *match) ;
Bool strokedpathsareadjacent(PATHINFO *path, SYSTEMVALUE dx, SYSTEMVALUE dy,
                             STROKE_PARAMS *sparams ) ;

Bool pathsplitsrect(PATHINFO *path1, PATHINFO *path2,
                    int32 type, int32 *rtype) ;

Bool pathisinsiderectanglepath(PATHINFO *path1, PATHINFO *path2, int32 type) ;

Bool pathsmatrixscale(PATHINFO *path1, PATHINFO *path2, OMATRIX *t_matrix) ;

#ifdef COMPLEX_BLENDS

Bool pathisverysimple(PATHINFO *path, Bool *degenerate,
                      int32 *orientation, int32 *type) ;
Bool pathissimple(PATHINFO *path, Bool *degenerate,
                  int32 *orientation, int32 *type) ;

Bool pathisinsidepath(PATHINFO *path1, PATHINFO *path2) ;
Bool pointofpathinsidepath(PATHINFO *path1, PATHINFO *path2) ;

Bool pathsintersect(PATHINFO *path1, PATHINFO *path2, Bool checkjoins) ;
Bool strokedpathsintersect(PATHINFO *path1, PATHINFO *path2, Bool checkjoins) ;

#endif

#endif /* protection for multiple inclusion */


/* Log stripped */
