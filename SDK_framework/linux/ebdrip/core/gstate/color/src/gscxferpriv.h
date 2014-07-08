/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscxferpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Transfer functions private API
 */

#ifndef __GSCXFERPRIV_H__
#define __GSCXFERPRIV_H__


#include "mps.h"                /* mps_ss_t */

#include "gs_colorprivt.h"      /* GS_CONSTRUCT_CONTEXT */
#include "gscxfer.h"

#define CLID_SIZEtransfer       3
#define CLID_SIZEdummytransfer  3

CLINK *cc_transfer_create(int32             nColorants,
                          COLORANTINDEX     *colorants,
                          COLORSPACE_ID     colorSpace,
                          uint8             reproType,
                          int32             jobColorSpaceIsGray,
                          int32             isFirstTransferLink,
                          int32             isFinalTransferLink,
                          int32             applyJobPolarity,
                          GS_TRANSFERinfo   *transferInfo,
                          GS_HALFTONEinfo   *halftoneInfo,
                          GUCR_RASTERSTYLE  *rasterstyle,
                          Bool              forcePositiveEnabled,
                          COLOR_PAGE_PARAMS *colorPageParams);
CLINK *cc_dummy_transfer_create( int32           nColorants ,
                                 COLORANTINDEX   *colorants ,
                                 COLORSPACE_ID   colorSpace ,
                                 int32           isFirstTransferLink ,
                                 int32           isFinalTransferLink ,
                                 int32           applyJobPolarity ,
                                 Bool            negativeJob );

void  cc_destroytransferinfo( GS_TRANSFERinfo **transferInfo ) ;

void  cc_reservetransferinfo( GS_TRANSFERinfo *transferInfo );

Bool cc_aretransferobjectslocal(corecontext_t *corecontext,
                                GS_TRANSFERinfo *transferInfo );

mps_res_t cc_scan_transfer( mps_ss_t ss, GS_TRANSFERinfo *transferInfo );

Bool cc_sametransferhalftoneinfo (void * pvHalftoneInfo, CLINK * pLink);
Bool cc_sametransferinfo (void * pvTransferInfo, CLINK * pLink);

/* Log stripped */

#endif /* __GSCXFERPRIV_H__ */
