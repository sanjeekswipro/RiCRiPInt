/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlcursor.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Publishes PCLXL (level) cursor move function(s)
 */

#ifndef __PCLXL_CURSOR_H__
#define __PCLXL_CURSOR_H__ 1

#include "pclxltypes.h"
#include "pclxlparsercontext.h"
#include "pclxlgraphicsstatet.h"

int32 pclxl_moveto(PCLXL_CONTEXT pclxl_context,
                   PCLXL_SysVal x,
                   PCLXL_SysVal y);

int32 pclxl_moveif(PCLXL_CONTEXT pclxl_context,
                   PCLXL_SysVal x,
                   PCLXL_SysVal y);

#endif /* __PCLXL_CURSOR_H__ */

/******************************************************************************
* Log stripped */
