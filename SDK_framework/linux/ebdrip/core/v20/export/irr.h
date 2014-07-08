/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:irr.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2012-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Internal Retained Raster
 */

#ifndef __IRR_H__
#define __IRR_H__

#include "dlstate.h"

typedef struct irr_state_t irr_state_t;
typedef struct irr_store_t irr_store_t;

size_t irr_store_footprint(irr_store_t *store);

void irr_store_free(irr_store_t **free_store);

Bool irr_read_back_band(irr_store_t *store,
                        int32 separation, int32 frame_and_bandnum,
                        uint8 *buff, int32 len, Bool do_copy, Bool *do_erase);

struct core_init_fns;
void irr_pgb_C_globals(struct core_init_fns *fns);

Bool irr_pgb_install(DL_STATE *page);

void irr_pgb_remove(DL_STATE *page);

Bool irr_render_complete(DL_STATE *page, Bool result);

void irr_sepdetect_flags(DL_STATE *page, Bool *page_is_composite,
                         Bool *page_is_separations, Bool *multiple_separations);

void irr_omit_blank_separations(DL_STATE *page);

Bool irr_setdestination(DL_STATE *page, mm_pool_t pool, dbbox_t *bbox,
                        void *priv_context, uintptr_t priv_id,
                        Bool (*callback)(void *priv_context,
                                         uintptr_t priv_id,
                                         irr_store_t *store));

void irr_free(DL_STATE *dl_state);

uintptr_t irr_getprivid(irr_state_t *state);

void irr_addtodl(DL_STATE *page, irr_store_t *store);

#endif /* protection for multiple inclusion */

/* Log stripped */
