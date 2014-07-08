/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!export:rcbshfil.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines for recombination of shfill objects. Shfill objects are recombined
 * by storing the high-level coordinate, color, and function information
 * during the initial passes, and expanding these into a full shfill object
 * in the adjustment phase.
 */

#ifndef __RCBSHFIL_H__
#define __RCBSHFIL_H__

/* Definitions in other compounds */
struct SHADINGinfo ;
struct SHADINGvertex ;
struct SHADINGtensor ;

#include "displayt.h"

/* Opaque handle for recombine linearised functions */
typedef struct rcbs_function_t *rcbs_function_h ;

/* When allocating arrays of function handles, allocate in blocks of 4. This
   is a compromise between frequent re-allocation when merging (which will
   cause fragmentation of the DL pool), and memory wastage. The number 4 is
   chosen so that CMYK jobs with no extra spots will not need any
   reallocation */
#define RCBS_FUNC_HARRAY_SPACE(_n) \
        ((((_n) + 3) & ~3) * (sizeof(rcbs_function_h) + sizeof(COLORANTINDEX)))
#define RCBS_FUNC_CINDEX_OFFSET(_n) \
        ((((_n) + 3) & ~3) * sizeof(rcbs_function_h))

/* Storing functions */
Bool rcbs_store_function(struct SHADINGvertex *corners[4],
                         struct SHADINGinfo *sinfo) ;
Bool rcbs_store_blend(SYSTEMVALUE coords[], USERVALUE colors[2],
                      Bool extend[2], struct SHADINGinfo *sinfo) ;
Bool rcbs_store_gouraud(struct SHADINGvertex *v1, struct SHADINGvertex *v2,
                        struct SHADINGvertex *v3, struct SHADINGinfo *sinfo) ;
Bool rcbs_store_tensor(struct SHADINGtensor *tensor, struct SHADINGinfo *sinfo) ;

/* Deleting functions */
void rcbs_free_patch(rcbs_patch_t **patchh, DL_STATE *page);

/* Info functions */
int32 rcbs_patch_type(rcbs_patch_t *patch);
void rcbs_bbox(rcbs_patch_t *patch, dbbox_t *bbox);

/* Comparison functions */
Bool rcbs_compare_function(rcbs_patch_t *patch1, rcbs_patch_t *patch2);
Bool rcbs_compare_axial(rcbs_patch_t *patch1, rcbs_patch_t *patch2);
Bool rcbs_compare_radial(rcbs_patch_t *patch1, rcbs_patch_t *patch2);
Bool rcbs_compare_gouraud(rcbs_patch_t *patch1, rcbs_patch_t *patch2);
Bool rcbs_compare_tensor(rcbs_patch_t *patch1, rcbs_patch_t *patch2);

/* Merging functions */
Bool rcbs_merge_shfill(LISTOBJECT *lobj_old, LISTOBJECT *lobj_new) ;

/* Adjusting functions */
Bool rcbs_adjust_function(rcbs_patch_t *patch, struct SHADINGinfo *sinfo);
Bool rcbs_adjust_axial(rcbs_patch_t *patch, struct SHADINGinfo *sinfo);
Bool rcbs_adjust_radial(rcbs_patch_t *patch, struct SHADINGinfo *sinfo);
Bool rcbs_adjust_gouraud(rcbs_patch_t *patch, struct SHADINGinfo *sinfo,
                         COLORANTINDEX *colorants);
Bool rcbs_adjust_tensor(rcbs_patch_t *patch, struct SHADINGinfo *sinfo,
                        COLORANTINDEX *colorants);

Bool rcbs_adjust_fn_order(struct SHADINGinfo *sinfo, COLORANTINDEX *colorants) ;

/* Linearised function support */
Bool rcbs_fn_evaluate_with_direction(rcbs_function_h lfnhandle,
                                     USERVALUE *inputs,
                                     USERVALUE *outputs,
                                     Bool upwards) ;

Bool rcbs_fn_find_discontinuity(rcbs_function_h lfnhandle,
                                int32 dimen,
                                USERVALUE bounds[2],
                                USERVALUE *discont,
                                int32 *order) ;

void rcbs_fn_free(rcbs_function_h *lfnhandle, DL_STATE *page) ;

Bool rcbs_fn_linearise(rcbs_function_h *linearised_fn,
                       int32 cindex, USERVALUE *domain,
                       struct SHADINGinfo *sinfo, int32 offset) ;


/*
* Log stripped */
#endif
