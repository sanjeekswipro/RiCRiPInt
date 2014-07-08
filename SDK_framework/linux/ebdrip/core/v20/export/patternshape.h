/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!export:patternshape.h(EBDSDK_P.1) $
 * $Id: export:patternshape.h,v 1.12.2.1.1.1 2013/12/19 11:25:24 anon Exp $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * pattern_shape_t stores the pattern shape mask for recursive patterns, and
 * the key cell clip for all patterns.
 */

#ifndef __PATTERNSHAPE_H__
#define __PATTERNSHAPE_H__

/* displayt.h for pattern_shape_t forward declaration */
#include "displayt.h"

DlSSEntry *patternshape_copy(DlSSEntry *entry, mm_pool_t *pools);

void patternshape_delete(DlSSEntry* entry, mm_pool_t *pools);

uintptr_t patternshape_hash(DlSSEntry* entry) ;

Bool patternshape_same(DlSSEntry *entryA, DlSSEntry *entryB) ;

Bool patternshape_lookup(DL_STATE *page,
                         STATEOBJECT *currentstate, STATEOBJECT* newstate) ;

Bool patternshape_finishdl(DL_STATE *page, PATTERNOBJECT *patobj) ;

form_array_t *patternshape_clipform(pattern_shape_t *shape) ;

form_array_t *patternshape_maskform(pattern_tracker_t *top_pattern,
                                    pattern_tracker_t *relative_pattern) ;

const dbbox_t *patternshape_bbox(pattern_shape_t *shape) ;

#if defined( DEBUG_BUILD )

void init_patternshape_debug(void) ;

#endif

/* =============================================================================
* Log stripped */

#endif /* protection for multiple inclusion */
