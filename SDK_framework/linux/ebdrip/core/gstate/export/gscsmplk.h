/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gscsmplk.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Simple device space conversion link functions.
 */

#ifndef __GSCSMPLK_H__
#define __GSCSMPLK_H__

#include "gs_color.h"

OBJECT *gsc_getblackgenerationobject( GS_COLORinfo *colorInfo ) ;
int32   gsc_getblackgenerationid( GS_COLORinfo *colorInfo ) ;
OBJECT *gsc_getundercolorremovalobject( GS_COLORinfo *colorInfo ) ;
int32   gsc_getundercolorremovalid( GS_COLORinfo *colorInfo ) ;

Bool gsc_setblackgeneration(corecontext_t *corecontext,
                            GS_COLORinfo  *colorInfo,
                            STACK         *pstack) ;
Bool gsc_setundercolorremoval(corecontext_t *corecontext,
                              GS_COLORinfo  *colorInfo,
                              STACK         *pstack) ;


#endif /* __GSCSMPLK_H__ */

/* Log stripped */
