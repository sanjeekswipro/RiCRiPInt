/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gschtone.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS halftone operators
 */

#ifndef __GSCHTONE_H__
#define __GSCHTONE_H__

#include "gs_color.h"
#include "objecth.h"


int32 gsc_setdefaulthalftoneinfo( GS_COLORinfo *colorInfo,
                                  GS_COLORinfo *defaultColorInfo );
SPOTNO gsc_getSpotno( GS_COLORinfo *colorInfo );
Bool gsc_setSpotno(GS_COLORinfo *colorInfo, SPOTNO spotno);

uint8  gsc_getscreentype( GS_COLORinfo *colorInfo );
HALFTONE *gsc_gethalftones( GS_COLORinfo *colorInfo );
OBJECT *gsc_gethalftonedict( GS_COLORinfo *colorInfo );
int32  gsc_gethalftoneid( GS_COLORinfo *colorInfo );

int32 gsc_setscreens( GS_COLORinfo *colorInfo, STACK *stack, int32 screenType );
int32 gsc_currentscreens( corecontext_t *context, GS_COLORinfo *colorInfo,
                          STACK *stack, int32 i , int32 j );
int32 gsc_currenthalftones ( GS_COLORinfo *colorInfo, STACK *stack );

int32 gsc_redo_setscreen( GS_COLORinfo *colorInfo ) ;
uint8 gsc_regeneratescreen( GS_COLORinfo *colorInfo ) ;
void gsc_invalidate_one_gstate_screens( GS_COLORinfo *colorInfo, uint8 regenscreen ) ;

OBJECT *ht_extract_spotfunction( OBJECT *htdict , NAMECACHE *color );
OBJECT *gs_extract_spotfunction( SPOTNO spotno, COLORANTINDEX ci,
                                 NAMECACHE *colorname );

Bool gsc_regeneratehalftoneinfo( GS_COLORinfo *colorInfo,
                                 SPOTNO fromspotno, SPOTNO tospotno );


#endif /* __GSCHTONE_H__ */

/* Log stripped */
