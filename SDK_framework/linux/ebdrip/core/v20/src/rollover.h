/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:rollover.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to rollover (a.k.a. hidden fill) functions.
 */

#ifndef __ROLLOVER_H__
#define __ROLLOVER_H__

#include "display.h"

/*---------------------------------------------------------------------------
 *             HEURISTIC PSEUDO-VIGNETTE RENDERING OPTIMISATIONS
 *                       aka ROLLOVER FILLS
 *---------------------------------------------------------------------------*/

void dlobj_rollover(DL_STATE *page, LISTOBJECT *lobj, HDL *hdl);

#endif /* protection for multiple inclusion */

/* Log stripped */
