/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscluminositypriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Luminosity private data
 */

#ifndef __GSCLUMINOSITYPRIV_H__
#define __GSCLUMINOSITYPRIV_H__

#include "gscluminosity.h"

CLINK* cc_luminosity_create(GS_CONSTRUCT_CONTEXT  *context);

int32 cc_luminosity_ncomps(void);
COLORANTINDEX* cc_luminosity_colorants(void);


/* Log stripped */

#endif /* __GSCLUMINOSITYPRIV_H__ */
