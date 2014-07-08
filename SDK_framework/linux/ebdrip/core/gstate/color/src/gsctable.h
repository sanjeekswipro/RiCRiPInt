/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gsctable.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Table based color spaces.
 */

#ifndef __GSCTABLE_H__
#define __GSCTABLE_H__

#include "gs_colorpriv.h"       /* CLINKcietablea */
#include "gschcmspriv.h"        /* TRANSFORM_LINK_INFO */

CLINK *cc_cietablea_create(CLINKcietablea     *cietableaInfo,
                           COLORSPACE_ID      *outputColorSpaceId,
                           int32              *outputDimensions);

CLINK *cc_cietableabc_create(CLINKcietableabc   *cietableabcInfo,
                             COLORSPACE_ID      *outputColorSpaceId,
                             int32              *outputDimensions);

CLINK *cc_cietableabcd_create(CLINKcietableabcd  *cietableabcdInfo,
                              COLORSPACE_ID      *outputColorSpaceId,
                              int32              *outputDimensions);

void cc_getCieTableRange( CLINKcietableabcd *cietableabcdInfo, int32 index, SYSTEMVALUE range[2] );


Bool cc_createcietableainfo( CLINKcietablea   **cietableaInfo,
                             COLORSPACE_ID    *outputColorSpaceId,
                             OBJECT           *colorSpaceObject,
                             GS_COLORinfo     *colorInfo );

void cc_destroycietableainfo( CLINKcietablea  **cietableaInfo );

void cc_reservecietableainfo( CLINKcietablea  *cietableaInfo );

TRANSFORM_LINK_INFO *cc_nextCIETableAInfo(CLINKcietablea  *cietableaInfo);

int32 cc_cietablea_nOutputChannels(CLINKcietablea  *cietableaInfo);


Bool cc_createcietableabcinfo( CLINKcietableabc  **cietableabcInfo,
                               COLORSPACE_ID     *outputColorSpaceId,
                               OBJECT            *colorSpaceObject,
                               GS_COLORinfo      *colorInfo );

void cc_destroycietableabcinfo( CLINKcietableabc  **cietableabcInfo );

void cc_reservecietableabcinfo( CLINKcietableabc  *cietableabcInfo );

TRANSFORM_LINK_INFO *cc_nextCIETableABCInfo(CLINKcietableabc  *cietableabcInfo);

int32 cc_cietableabc_nOutputChannels(CLINKcietableabc  *cietableabcInfo);


Bool cc_createcietableabcdinfo( CLINKcietableabcd  **cietableabcdInfo,
                                COLORSPACE_ID      *outputColorSpaceId,
                                OBJECT             *colorSpaceObject,
                                GS_COLORinfo       *colorInfo );

void cc_destroycietableabcdinfo( CLINKcietableabcd  **cietableabcdInfo );

void cc_reservecietableabcdinfo( CLINKcietableabcd  *cietableabcdInfo );

TRANSFORM_LINK_INFO *cc_nextCIETableABCDInfo(CLINKcietableabcd  *cietableabcdInfo);

int32 cc_cietableabcd_nOutputChannels(CLINKcietableabcd  *cietableabcdInfo);


#endif /* __GSCTABLE_H__ */

/* Log stripped */
