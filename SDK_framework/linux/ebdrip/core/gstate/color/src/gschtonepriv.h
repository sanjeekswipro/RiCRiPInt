/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gschtonepriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Colour and halftones interface private data.
 */

#ifndef __GSCHTONEPRIV_H__
#define __GSCHTONEPRIV_H__

#include "gs_colorpriv.h" /* GS_HALFTONEinfo */
#include "graphict.h" /* GUCR_RASTERSTYLE */
#include "mps.h"
#include "gschtone.h" /* by convention */


#if defined( ASSERT_BUILD )
extern Bool debug_regeneration;
#endif

void cc_destroyhalftoneinfo( GS_HALFTONEinfo **halftoneInfo ) ;
void cc_reservehalftoneinfo( GS_HALFTONEinfo *halftoneInfo );

/* pHalftoneOffset is an index 0..nColorants to use as a key in the fn cache */
Bool cc_halftonetransferinfo(GS_HALFTONEinfo *halftoneinfo,
                             COLORANTINDEX      iColorant,
                             HTTYPE             reproType,
                             OBJECT             **halftonetransfer,
                             int32              *pHalftoneOffset,
                             GUCR_RASTERSTYLE   *rasterstyle);
Bool cc_arehalftoneobjectslocal(corecontext_t *corecontext,
                                GS_HALFTONEinfo *halftoneInfo );
mps_res_t cc_scan_halftone( mps_ss_t ss, GS_HALFTONEinfo *halftoneInfo );

int32 cc_gethalftoneid(GS_HALFTONEinfo *halftoneInfo);

/* Log stripped */

#endif /* __GSCHTONEPRIV_H__ */
