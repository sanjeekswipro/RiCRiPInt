/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbvmerg.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine vignette merging
 */

#ifndef __RCBVMERG_H__
#define __RCBVMERG_H__

#include "rcbcomp.h"

Bool rcbv_merge_vignettes( DL_STATE *page,
                           rcbv_compare_t *rcbv_compare ,
                           LISTOBJECT *lobj_old ,
                           LISTOBJECT *lobj_new ,
                           LISTOBJECT **lobj_ret ,
                           Bool *faddnewtodl ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
