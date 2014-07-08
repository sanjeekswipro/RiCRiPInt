/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gsccrd.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Color Rendering Dictionary (CRD) API
 */

#ifndef __GSCCRD_H__
#define __GSCCRD_H__

#include "gs_color.h" /* GS_COLORinfo */

Bool gsc_setcolorrendering(GS_COLORinfo *colorInfo, STACK *stack);
OBJECT *gsc_getcolorrendering(GS_COLORinfo *colorInfo);

#endif /* __GSCCRD_H__ */

/* Log stripped */
