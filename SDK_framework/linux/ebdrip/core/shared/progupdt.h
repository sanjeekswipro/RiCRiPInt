/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:progupdt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Progress update API.
 */

#ifndef __PROGUPDT_H__
#define __PROGUPDT_H__ (1)

#include "timelineapi.h" /* sw_tl_type etc. */

struct core_init_fns ; /* SWcore */

/** Create a timeline and start reporting progress for named thing.
 */
sw_tl_ref progress_start(/*@null@*/ /*@in@*/ const uint8 *name, size_t length,
                         sw_tl_extent extent);

/** Report the current progress for the thing. */
void progress_current(sw_tl_ref tl_ref, sw_tl_extent current);

/** End reporting progress for the thing. */
void progress_end(/*@notnull@*/ /*@in@*/ sw_tl_ref *tl_ref);

/** Flag reporting a request to update progress information */
extern
Bool do_progress_updates;

/** Initialise progress reporting globals */
void progress_C_globals(struct core_init_fns*  fns);

#endif /* !__PROGUPDT_H__ */

/* Log stripped */
