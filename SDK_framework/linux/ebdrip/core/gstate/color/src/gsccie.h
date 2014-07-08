/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gsccie.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color chain links for cie based color spaces.
 */

#ifndef __GSCCIE_H__
#define __GSCCIE_H__


#include "gs_colorpriv.h"       /* CLINKciebaseda */
#include "gschcmspriv.h"        /* TRANSFORM_LINK_INFO */

CLINK *cc_ciebaseda_create(CLINKciebaseda     *ciebasedaInfo,
                           XYZVALUE           **sourceWhitePoint,
                           XYZVALUE           **sourceBlackPoint,
                           XYZVALUE           **sourceRelativeWhitePoint,
                           XYZVALUE           **sourceRelativeBlackPoint);

CLINK *cc_ciebasedabc_create(CLINKciebasedabc   *ciebasedabcInfo,
                             XYZVALUE           **sourceWhitePoint,
                             XYZVALUE           **sourceBlackPoint,
                             XYZVALUE           **sourceRelativeWhitePoint,
                             XYZVALUE           **sourceRelativeBlackPoint);

CLINK *cc_ciebaseddef_create(CLINKciebaseddef   *ciebaseddefInfo);

CLINK *cc_ciebaseddefg_create(CLINKciebaseddefg  *ciebaseddefgInfo);

void cc_getCieBasedABCRange( CLINKciebasedabc *ciebasedabcInfo,
                             int32 index,
                             SYSTEMVALUE range[2] );
void cc_getCieBasedDEFRange( CLINKciebaseddef *ciebaseddefInfo,
                             int32 index,
                             SYSTEMVALUE range[2] );
void cc_getCieBasedDEFGRange( CLINKciebaseddefg *ciebaseddefgInfo,
                              int32 index,
                              SYSTEMVALUE range[2] );

void cc_getCieBasedABCWhitePoint( CLINK *pLink, XYZVALUE whitepoint);
void cc_getCieBasedABCBlackPoint( CLINK *pLink, XYZVALUE blackpoint);

Bool cc_createciebasedainfo( CLINKciebaseda     **ciebasedaInfo,
                             OBJECT             *colorSpaceObject );
void  cc_destroyciebasedainfo( CLINKciebaseda   **ciebasedaInfo );
void  cc_reserveciebasedainfo( CLINKciebaseda *ciebasedaInfo );


Bool cc_createciebasedabcinfo( CLINKciebasedabc   **ciebasedabcInfo,
                               OBJECT             *colorSpaceObject );
void  cc_destroyciebasedabcinfo( CLINKciebasedabc **ciebasedabcInfo );

void  cc_reserveciebasedabcinfo( CLINKciebasedabc *ciebasedabcInfo );

Bool cc_get_isPhotoshopRGB(CLINKciebasedabc *ciebasedabcInfo);

Bool cc_createciebaseddefinfo( CLINKciebaseddef   **ciebaseddefInfo,
                               COLORSPACE_ID      *outputColorSpaceId,
                               OBJECT             *colorSpaceObject );
void  cc_destroyciebaseddefinfo( CLINKciebaseddef **ciebaseddefInfo );

void  cc_reserveciebaseddefinfo( CLINKciebaseddef *ciebaseddefInfo );

TRANSFORM_LINK_INFO *cc_nextCIEBasedDEFInfo(CLINKciebaseddef *ciebaseddefInfo);



Bool cc_createciebaseddefginfo( CLINKciebaseddefg  **ciebaseddefgInfo,
                                COLORSPACE_ID      *outputColorSpaceId,
                                OBJECT             *colorSpaceObject );
void  cc_destroyciebaseddefginfo( CLINKciebaseddefg  **ciebaseddefgInfo );

void  cc_reserveciebaseddefginfo( CLINKciebaseddefg *ciebaseddefgInfo );

TRANSFORM_LINK_INFO *cc_nextCIEBasedDEFGInfo(CLINKciebaseddefg *ciebaseddefgInfo);


#endif /* __GSCCIE_H__ */

/* Log stripped */
