/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!export:imageadj.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image adjustment API
 */
#ifndef __IMAGEADJ_H__
#define __IMAGEADJ_H__

/* ---------------------------------------------------------------------- */

#include "displayt.h"       /* LISTOBJECT */
#include "graphict.h"       /* GS_COLORinfo */

/**
 * Preconvert the image for direct-rendered regions.
 *
 * If replace is set, the image is not required for compositing and the image
 * can be converted and replaced.  If replace is clear, the image may still be
 * required for compositing and therefore an alternate image expander with a new
 * LUT is prepared if possible, but if not the image will need converting
 * on-the-fly.
 */
Bool im_adj_adjustimage(DL_STATE *page, Bool replace,
                        LISTOBJECT *image_lobj, USERVALUE *colorvalues,
                        COLORANTINDEX *inputcolorants, GS_COLORinfo *colorInfo,
                        int32 colortype, int32 method);

#endif /* protection for multiple inclusion */

/* Log stripped */
