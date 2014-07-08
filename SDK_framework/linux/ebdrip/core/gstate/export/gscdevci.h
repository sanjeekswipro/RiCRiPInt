/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gscdevci.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Graphic-state PS level 1 device-dependent operators
 */

#ifndef __GSCDEVCI_H__
#define __GSCDEVCI_H__

#include "graphict.h"           /* GS_COLORinfo */

struct DL_STATE;
struct dl_color_t;

/* Overprint userparams are mirrored in the gstate. Use these definitions to force
 * gstate values to match userparams by default.
 */
#define INITIAL_OVERPRINT_MODE              (TRUE)
#define INITIAL_OVERPRINT_BLACK             (FALSE)
#define INITIAL_OVERPRINT_GRAY              (FALSE)
#define INITIAL_OVERPRINT_GRAYIMAGES        (TRUE)
#define INITIAL_OVERPRINT_WHITE             (FALSE)
#define INITIAL_IGNORE_OVERPRINT_MODE       (FALSE)
#define INITIAL_TRANSFORMED_SPOT_OVERPRINT  (FALSE)
#define INITIAL_OVERPRINT_ICCBASED          (FALSE)

Bool gsc_enableOverprint(GS_COLORinfo *colorInfo);
Bool gsc_disableOverprint(GS_COLORinfo *colorInfo);
Bool gsc_setoverprint( GS_COLORinfo *colorInfo, int32 colorType, Bool overprint ) ;
Bool gsc_getoverprint( GS_COLORinfo *colorInfo, int32 colorType ) ;
Bool gsc_setoverprintmode( GS_COLORinfo *colorInfo, Bool overprintMode ) ;
Bool gsc_getoverprintmode( GS_COLORinfo *colorInfo ) ;
Bool gsc_setcurrentoverprintmode( GS_COLORinfo *colorInfo, Bool overprintMode ) ;
Bool gsc_getcurrentoverprintmode( GS_COLORinfo *colorInfo ) ;
Bool gsc_setoverprintblack( GS_COLORinfo *colorInfo, Bool overprintBlack ) ;
Bool gsc_getoverprintblack( GS_COLORinfo *colorInfo ) ;
Bool gsc_setoverprintgray( GS_COLORinfo *colorInfo, Bool overprintGray ) ;
Bool gsc_getoverprintgray( GS_COLORinfo *colorInfo ) ;
Bool gsc_setoverprintgrayimages( GS_COLORinfo *colorInfo, Bool overprintGrayImages ) ;
Bool gsc_getoverprintgrayimages( GS_COLORinfo *colorInfo ) ;
Bool gsc_setoverprintwhite( GS_COLORinfo *colorInfo, Bool overprintWhite ) ;
Bool gsc_getoverprintwhite( GS_COLORinfo *colorInfo ) ;
Bool gsc_setignoreoverprintmode( GS_COLORinfo *colorInfo, Bool ignoreOverprintMode ) ;
Bool gsc_getignoreoverprintmode( GS_COLORinfo *colorInfo ) ;
Bool gsc_settransformedspotoverprint( GS_COLORinfo *colorInfo, Bool transformedSpotOverprint ) ;
Bool gsc_gettransformedspotoverprint( GS_COLORinfo *colorInfo ) ;
Bool gsc_setoverprinticcbased( GS_COLORinfo *colorInfo, Bool transformedSpotOverprint ) ;
Bool gsc_getoverprinticcbased( GS_COLORinfo *colorInfo ) ;

Bool gsc_isoverprintpossible(GS_COLORinfo *colorInfo, int32 colorType) ;

Bool gsc_setblockoverprints(GS_COLORinfo *colorInfo, int32 colorType) ;
void gsc_clearblockoverprints(GS_COLORinfo *colorInfo, int32 colorType) ;
Bool gsc_applyblockoverprints(GS_COLORinfo *colorInfo, int32 colorType,
                              struct DL_STATE *page, struct dl_color_t *pdlc) ;

#endif /* __GSCDEVCI_H__ */

/* Log stripped */
