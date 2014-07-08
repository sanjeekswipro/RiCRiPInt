/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:pattern.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS pattern API
 */

#ifndef __PATTERN_H__
#define __PATTERN_H__

#include "graphict.h" /* GSTATE */
#include "gs_color.h" /* COLORSPACE_ID */
#include "stacks.h"   /* STACK */
#include "matrix.h"   /* OMATRIX */

typedef struct pattern_nonoverlap_t pattern_nonoverlap_t ;

/* Defines for pattern types */
enum {
  COLOURED_PATTERN = 1,    /* MUST match coloured pattern PaintType (1) */
  UNCOLOURED_PATTERN = 2,  /* MUST match uncoloured pattern PaintType (2) */
  COLOURED_TRANSPARENT_PATTERN,
  UNCOLOURED_TRANSPARENT_PATTERN,
  LEVEL1SCREEN_PATTERN,
  NO_PATTERN
} ;

#define INVALID_PATTERN_ID 0

#if defined( ASSERT_BUILD )
extern Bool debug_pattern ;
#endif

Bool patterncheck(DL_STATE *page, GSTATE *pgstate, int32 colorType,
                  int32 *patternid, int32 *parent_pid,
                  int32 *patterntype, TranAttrib *ta) ;

Bool fTreatScreenAsPattern (COLORSPACE_ID      colorSpace,
                            int32              colorType,
                            GUCR_RASTERSTYLE   *hRasterStyle,
                            int32              spotno);

/* **** WARNING ****
   The order of the pattern implementation array is significant!
   The implementation array can be read by customers (i.e. Rampage)
   who rely on the order being maintained, unless there's a very good
   reason to change it.  Add new items prior to PIA_CHECKSUM and
   PATTERN_IMPLEMENTATION_SIZE, which must be last.
   pia must match the 'pia_types' array in pattern.c.
 */
/* Pattern Implementation array access macros, and validity checking */
enum pia   { PIA_PID = 0,
             PIA_GSTATE, 
             PIA_PAINTTYPE, PIA_TILINGTYPE,
             PIA_XSTEP, PIA_YSTEP,
             PIA_LLX, PIA_LLY, PIA_URX, PIA_URY,
	     PIA_MATRIX,
             PIA_MATRIX_USPACE, PIA_MATRIX_USPACE_REL,
             PIA_CONTEXT_PID,
             PIA_CHECKSUM,
             PATTERN_IMPLEMENTATION_SIZE } ;

Bool check_pia_valid( OBJECT *impl ) ;

Bool getPatternId(GS_COLORinfo *colorInfo,
                  int32        colorType,
                  int32        *patternId,
                  int32        *paintType);

HDL *patternHdl(PATTERNOBJECT *patobj);

Bool gs_makepattern( STACK *stack , OBJECT *newd ) ;

Bool pattern_matrix_remake(int32 colorType, OMATRIX *newmatrix, Bool absolute) ;

Bool pattern_executingpaintproc(int32 *id) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
