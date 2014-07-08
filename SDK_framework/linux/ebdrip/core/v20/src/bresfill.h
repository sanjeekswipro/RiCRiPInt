/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:bresfill.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definition of Bressenham fill API
 */

#ifndef __BRESFILL_H__
#define __BRESFILL_H__

#include "ndisplay.h"           /* NFILLOBJECT */


int32 fillnbressdisplay(DL_STATE *page, int32 therule, NFILLOBJECT *nfill);

/* spaceinverted indicates whether mirrorprint or equiv in force */
int32 accfillnbressdisplay(DL_STATE *page, int32 therule, NFILLOBJECT *nfill,
                           uint32 flags);

#endif /* protection for multiple inclusion */

/* Log stripped */
