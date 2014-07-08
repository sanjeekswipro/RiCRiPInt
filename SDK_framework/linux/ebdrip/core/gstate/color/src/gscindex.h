/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscindex.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS3: Indexed colorspace.
 */

#ifndef __GSCINDEX_H__
#define __GSCINDEX_H__

CLINK *cc_indexed_create(GS_COLORinfo       *colorInfo,
                         OBJECT             *PSColorspace,
                         GUCR_RASTERSTYLE   *hRasterStyle,
                         OBJECT             **basePSColorspace);

int16 cc_getindexedhival( CLINK *pLink );

#endif /* __GSCINDEX_H__ */

/* Log stripped */
