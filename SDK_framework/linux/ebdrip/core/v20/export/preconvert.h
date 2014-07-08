/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:preconvert.h(EBDSDK_P.1) $
 * $Id: export:preconvert.h,v 1.14.1.1.1.1 2013/12/19 11:25:19 anon Exp $
 *
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The preconvert code color converts DL objects from their blend spaces to
 * device space ready for direct rendering.
 */

#ifndef __PRECONVERT_H__
#define __PRECONVERT_H__

typedef struct Preconvert Preconvert;

Bool preconvert_new(DL_STATE *page, GS_COLORinfo *colorInfo, OBJECT colorSpace,
                    COLORSPACE_ID processSpace, uint32 nProcessComps,
                    GUCR_RASTERSTYLE *inputRS, Preconvert **newPreconvert);

void preconvert_free(DL_STATE *page, Preconvert **freePreconvert);

Bool preconvert_update(const DL_STATE *page, Preconvert *preconvert);

int32 preconvert_method(Preconvert *preconvert);

Bool preconvert_overprint_simplify(LISTOBJECT *lobj);

Bool preconvert_dlcolor(Group *group, int32 colorType, SPOTNO spotno,
                        uint8 reproType, LateColorAttrib *lateColorAttrib,
                        Bool overprint, Bool overprintSimplify,
                        dl_color_t *dlc);

Bool preconvert_invoke_all_colorants(Group *group, int32 colorType,
                                     uint8 reproType, LateColorAttrib *lateColorAttrib);

Bool preconvert_probe(Group *group, int32 colorType, SPOTNO spotno,
                      uint8 reproType, dl_color_t *dlc,
                      LateColorAttrib *lca);

Bool preconvert_on_the_fly(Group *group, LISTOBJECT *lobj, int32 colorType,
                           dl_color_t *dlc, dl_color_t *dlcResult);

Bool preconvert_dl(DL_STATE *dl, int32 transparency_strategy);

Bool preconvert_lobj_color_to_devicespace(Group *group, LISTOBJECT *lobj,
                                          dl_color_t *dlc);

#endif /* __PRECONVERT_H__ */

/* Log stripped */
