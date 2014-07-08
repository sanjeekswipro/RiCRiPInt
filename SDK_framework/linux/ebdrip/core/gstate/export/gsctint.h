/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gsctint.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The tint transform color link.
 */

#ifndef __GSCTINT_H__
#define __GSCTINT_H__


#include "objects.h"        /* OBJECT */

Bool gsc_invokeNamedColorIntercept(GS_COLORinfo *colorInfo,
                                   OBJECT       *namesObject,
                                   Bool         *fgotIntercept,
                                   Bool         f_return_results,
                                   OBJECT       *tinttransform,
                                   OBJECT       *alternativespace,
                                   int32        *tintTransformId);


#endif /* __GSCTINT_H__ */

/* Log stripped */
