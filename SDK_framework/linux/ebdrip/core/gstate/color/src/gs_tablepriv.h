/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gs_tablepriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color conversion (Tom's) tables API.
 */

#ifndef __GS_TABLEPRIV_H__
#define __GS_TABLEPRIV_H__

#include "gs_colorpriv.h"       /* GS_COLORinfo */
#include "gs_table.h"

GSC_TABLE *cc_createTomsTable(GS_COLORinfo *colorInfo, int32 colorType);

void cc_destroyTomsTable(GSC_TABLE *gst);

int32 cc_invokeTomsTable(GS_COLORinfo *colorInfo, int32 colorType,
                         int32 *piColorValues,
                         COLORVALUE *poColorValues,
                         int32 nColors,
                         GSC_TABLE *gst);

/** Color table init. */
Bool gst_swstart(void);

/** Color table finish. */
void gst_finish(void);

#endif /* __GS_TABLEPRIV_H__ */

/* Log stripped */
