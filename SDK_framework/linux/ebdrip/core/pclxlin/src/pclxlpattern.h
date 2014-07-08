/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpattern.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCLXL pattern interfaces.
 */
#ifndef _pclxlpattern_h_
#define _pclxlpattern_h_

#include "pclAttribTypes.h"
#include "pclxlcontext.h"
#include "pclxlimage.h"

/**
 * Initialise the pattern system; this will create the required pattern caches
 * and management structures.
 */
Bool pclxl_patterns_init(PCLXL_CONTEXT context);

/**
 * Finish the pattern system; this will destroy all pattern caches.
 */
void pclxl_patterns_finish(PCLXL_CONTEXT context);

/**
 * This method should be called when a new gstate is created; a pattern cache
 * will be created for the passed gstate.
 */
Bool pclxl_pattern_gstate_created(PCLXL_CONTEXT context,
                                  PCLXL_GRAPHICS_STATE gstate);

/**
 * This method should be called when a new gstate is destroyed.
 */
Bool pclxl_pattern_gstate_deleted(PCLXL_CONTEXT context,
                                  PCLXL_GRAPHICS_STATE gstate);

/**
 * Called at the end of a page.
 */
void pclxl_pattern_end_page(PCLXL_CONTEXT context);

/**
 * Called at the end of a session.
 */
void pclxl_pattern_end_session(PCLXL_CONTEXT context);

/**
 * This method should be called once page rendering is complete, and will purge
 * any zombie patterns from the pattern caches.
 */
void pclxl_pattern_rendering_complete(PCLXL_CONTEXT context);

/**
 * Given a pattern id, return the corresponding PclXLPattern.
 * If it's not present NULL is returned and it's up to the caller
 * to decide how to handle it.
 */
PclXLPattern* pclxl_pattern_find(PCLXL_CONTEXT context, int32 id);

/**
 * Free allocated pattern construction state members.
 *
 */
Bool pclxl_pattern_free_construction_state(PCLXL_PARSER_CONTEXT parser_context,
                                           Bool delete_cache_entry);

#endif

/* Log stripped */

