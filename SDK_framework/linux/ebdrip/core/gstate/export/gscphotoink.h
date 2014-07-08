/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!export:gscphotoink.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color chain private data
 */

#ifndef __GSCPHOTOINK_H__
#define __GSCPHOTOINK_H__

Bool guc_findPhotoinkColorantIndices(GUCR_RASTERSTYLE   *pRasterStyle,
                                     GUCR_PHOTOINK_INFO **pPhotoinkInfo);
void guc_destroyphotoinkinfo(GUCR_PHOTOINK_INFO **photoinkInfo);
Bool guc_photoink_colorant(const GUCR_RASTERSTYLE *pRasterStyle,
                           COLORANTINDEX          colorant);

#endif /* __GSCPHOTOINK_H__ */

/* Log stripped */
