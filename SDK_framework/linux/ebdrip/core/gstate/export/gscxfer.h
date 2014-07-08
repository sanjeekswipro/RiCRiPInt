/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gscxfer.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS transfer operators
 */

#ifndef __GSCXFER_H__
#define __GSCXFER_H__


#include "gs_color.h"     /* GS_COLORinfo */

OBJECT *gsc_gettransferobjects( GS_COLORinfo *colorInfo );
int32  gsc_gettransferid( GS_COLORinfo *colorInfo );

Bool gsc_analyze_for_forcepositive( corecontext_t *context,
                                    GS_COLORinfo *colorInfo, int32 colorType,
                                    Bool *forcePositive );

Bool gsc_settransfers(corecontext_t *corecontext,
                      GS_COLORinfo  *colorInfo,
                      STACK         *stack,
                      int32         nargs /* 1 or 4 */ );
Bool gsc_currenttransfers( GS_COLORinfo *colorInfo, STACK *stack, int32 i , int32 j );

Bool gsc_transfersPreapplied(GS_COLORinfo *colorInfo);

Bool gsc_setTransfersPreapplied(GS_COLORinfo *colorInfo, Bool preapplied);

#endif /* __GSCXFER_H__ */

/* Log stripped */
