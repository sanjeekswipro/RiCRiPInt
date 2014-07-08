/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gscphotoinkpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color chain private data
 */

#ifndef __GSCPHOTOINKPRIV_H__
#define __GSCPHOTOINKPRIV_H__

Bool guc_interpolatePhotoinkTransform(const GUCR_PHOTOINK_INFO  *photoinkInfo,
                                      COLORANTINDEX       *colorantMap,
                                      uint32              nMappedColorants,
                                      COLORVALUE          icolor,
                                      COLORVALUE          *ocolors);
Bool guc_photoink_colorant(const GUCR_RASTERSTYLE *pRasterStyle,
                           COLORANTINDEX colorant);

#endif /* __GSCPHOTOINKPRIV_H__ */

/* Log stripped */
