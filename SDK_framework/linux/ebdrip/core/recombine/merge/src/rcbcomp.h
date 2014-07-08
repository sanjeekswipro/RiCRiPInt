/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbcomp.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine compare API
 */

#ifndef __RCBCOMP_H__
#define __RCBCOMP_H__


#include "displayt.h"
#include "rcbcntrl.h"

typedef struct rcbv_compare_t {
  int32 matchtype;
  int32 matchstyle;
  int32 cextend1h;
  int32 cextend1t;
  int32 cextend2h;
  int32 cextend2t;
} rcbv_compare_t;

rcb_merge_t rcbv_compare_vignettes(rcbv_compare_t *rcbv_compare,
                                   LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                   rcb_merge_t test_type, Bool fmergevigko);

#endif /* protection for multiple inclusion */


/* Log stripped */
