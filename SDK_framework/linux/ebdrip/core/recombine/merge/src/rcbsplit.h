/** \file
 * \ingroup ps
 *
 * $HopeName: CORErecombine!merge:src:rcbsplit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine vignette splitting
 */

#ifndef __RCBSPLIT_H__
#define __RCBSPLIT_H__

int32 rcb_vignette_split_inline(DL_STATE *page,
                                LISTOBJECT *lobj, Bool updateDL);
void rcb_vignette_split_remove(DL_STATE *page,
                               LISTOBJECT *lobj);
void rcb_vignette_split_free(DL_STATE *page,
                             LISTOBJECT *lobj);
int32 rcb_vignette_split(DL_STATE *page,
                         LISTOBJECT *old_lobj, LISTOBJECT *new_lobj);

#endif /* protection for multiple inclusion */


/* Log stripped */
