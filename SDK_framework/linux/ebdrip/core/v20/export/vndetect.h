/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:vndetect.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Vignette detection API
 */

#ifndef __VNDETECT_H__
#define __VNDETECT_H__

#include  "vntypes.h"
#include  "displayt.h" /* LISTOBJECT, VIGNETTEOBJECT, DLREF */

struct PATHINFO ;
struct STROKE_PARAMS ;  /* from SWv20 */

/* ----- Exported macros ----- */

#define VD_DETECT( _test ) ((_test) && vd_detect())

/* ----- Exported functions ----- */

Bool vd_detect(void) ;

#if defined( DEBUG_BUILD )
void init_vignette_detection_debug( void ) ;
#endif

void  setup_analyze_vignette( void ) ;
void  reset_analyze_vignette( void ) ;

/** Cause of flush_vignette call. */
enum { VD_Default, VD_AddClip, VD_GRestore } ;

Bool flush_vignette(int32 cause) ;
void abort_vignette(DL_STATE *page) ;

Bool analyze_vignette_f(DL_STATE *page, struct PATHINFO *path,
                        int32 pathtype, int32 filltype,
                        Bool copythepath, int32 colorType) ;

Bool analyze_vignette_s(struct STROKE_PARAMS *sparams, int32 colorType) ;

Bool check_analyze_vignette_s(struct STROKE_PARAMS *sparams) ;

Bool analyzing_vignette(void) ;
void vn_preservestate(DL_STATE *page);

#endif /* protection for multiple inclusion */


/* Log stripped */
