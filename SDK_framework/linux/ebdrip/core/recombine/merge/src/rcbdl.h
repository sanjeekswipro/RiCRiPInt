/** \file
 * \ingroup ps
 *
 * $HopeName: CORErecombine!merge:src:rcbdl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * For the reorder and restitch routines for recombine DL mangling.
 */

#ifndef __RCBDL_H__
#define __RCBDL_H__

extern int32 rcb_reorder_dl_objects(DL_STATE *page, LISTOBJECT *old_lobj,
                                    int32 band, int32 *found) ;
extern int32 rcb_reorder_dl_objects_check(DL_STATE *page, LISTOBJECT *old_lobj,
                                          int32 band, int32 *fcheck) ;
extern int32 rcb_restitch_update(DL_STATE *page, LISTOBJECT *p_lobj_iv,
                                 LISTOBJECT *p_lobj_ko, int32 y1, int32 y2) ;
extern int32 rcb_restitch_check(DL_STATE *page, LISTOBJECT *p_lobj_iv,
                                LISTOBJECT *p_lobj_ko, int32 y1, int32 y2,
                                int32 *fcheck) ;
void rcb_remove_from_some_bands(DL_STATE *page, LISTOBJECT *lobj,
                                int32 b1, int32 b2) ;
void rcb_reassign_swapped_bands(DL_STATE *page, LISTOBJECT *old_lobj,
                                LISTOBJECT *new_lobj);
DLREF *rcb_find_lobj_pre(DL_STATE *page, int32 bandi, LISTOBJECT *lobj);

#if defined( ASSERT_BUILD )
int32 rcb_assert_not_on_dl(DL_STATE *page, LISTOBJECT *lobj, dbbox_t *bbox) ;
#endif

DLREF **rcb_dl_head_addr(DL_STATE *page, int32 bandi);
#define rcb_dl_head(_page, _bandi) (*rcb_dl_head_addr(_page, _bandi))
DLREF **rcb_dl_tails(DL_STATE *page, Bool snapshot);
void rcb_set_insertion(DL_STATE *page, int32 bandi, Bool snapshot, DLREF *dl);

#endif /* protection for multiple inclusion */

/* Log stripped */
