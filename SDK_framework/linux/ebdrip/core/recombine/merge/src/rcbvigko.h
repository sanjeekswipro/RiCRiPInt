/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbvigko.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine vignette knock-outs
 */

#ifndef __RCBVIGKO_H__
#define __RCBVIGKO_H__


#include "displayt.h"
#include "rcbcntrl.h"

rcb_merge_t rcbn_compare_vignette_knockout(LISTOBJECT *vlobj, LISTOBJECT *klobj) ;
Bool rcbn_fixVignetteKnockouts(DL_STATE *page) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
