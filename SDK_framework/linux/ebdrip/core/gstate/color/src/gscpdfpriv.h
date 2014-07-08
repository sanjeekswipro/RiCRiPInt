/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscpdfpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color spaces specific to pdf private data.
 */

#ifndef __GSCPDFPRIV_H__
#define __GSCPDFPRIV_H__


#include "objecth.h"      /* OBJECT */
#include "gs_color.h"     /* CLINKlab */
#include "gs_colorpriv.h" /* XYZVALUE */
#include "gscpdf.h"

CLINK *cc_lab_create(CLINKlab           *labInfo,
                     XYZVALUE           **sourceWhitePoint,
                     XYZVALUE           **sourceBlackPoint,
                     XYZVALUE           **sourceRelativeWhitePoint,
                     XYZVALUE           **sourceRelativeBlackPoint);

void cc_getLabRange( CLINKlab *labInfo, int32 index, SYSTEMVALUE range[2] );

int32 cc_createlabinfo( CLINKlab   **labInfo,
                        OBJECT     *colorSpaceObject );
void cc_destroylabinfo( CLINKlab       **labInfo );
void cc_reservelabinfo( CLINKlab *labInfo );


CLINK *cc_calgray_create(CLINKcalrgbg       *calgrayInfo,
                         XYZVALUE           **sourceWhitePoint,
                         XYZVALUE           **sourceBlackPoint,
                         XYZVALUE           **sourceRelativeWhitePoint,
                         XYZVALUE           **sourceRelativeBlackPoint);
CLINK *cc_calrgb_create(CLINKcalrgbg       *calrgbInfo,
                        XYZVALUE           **sourceWhitePoint,
                        XYZVALUE           **sourceBlackPoint,
                        XYZVALUE           **sourceRelativeWhitePoint,
                        XYZVALUE           **sourceRelativeBlackPoint);

int32 cc_createcalgrayinfo( CLINKcalrgbg  **calgrayInfo,
                            OBJECT        *colorSpaceObject );
void cc_destroycalgrayinfo( CLINKcalrgbg  **calgrayInfo );
void cc_reservecalgrayinfo( CLINKcalrgbg  *calgrayInfo );

int32 cc_createcalrgbinfo( CLINKcalrgbg  **calrgbInfo,
                           OBJECT        *colorSpaceObject );
void cc_destroycalrgbinfo( CLINKcalrgbg  **calrgbInfo );
void cc_reservecalrgbinfo( CLINKcalrgbg  *calrgbInfo );


/* Log stripped */

#endif /* __GSCPDFPRIV_H__ */
