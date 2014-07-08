/** \file
 * \ingroup renderloop
 *
 * $HopeName: CORErender!src:renderfn.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Render function dispatching according to object type, and some
 * rendering auxiliaries.
 */

#ifndef __RENDERFN_H__
#define __RENDERFN_H__ 1

/** \defgroup renderloop Render loop
    \ingroup rendering */
/** \{ */

#include "displayt.h"  /* N_RENDER_OPCODES */
#include "mlock.h" /* multi_lock_t */
#include "render.h" /* render_info_t */

struct PclAttrib ; /* from dodl/v20 */

/** Type definition for the object-specific render functions. */
typedef Bool (*RENDER_FUNCTION)(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                                Bool screened);

/** Table of render functions, one for each object. */
extern RENDER_FUNCTION renderfuncs[N_RENDER_OPCODES] ;

extern multi_rwlock_t nfill_lock;

#define NFILL_LOCK_WR_CLAIM(item) \
  multi_rwlock_lock(&nfill_lock, (void *)(item), MULTI_RWLOCK_WRITE)
#define NFILL_LOCK_RELEASE() multi_rwlock_unlock(&nfill_lock)


/** Is object selected for a modular halftoning mask? */
Bool mht_selected(const render_info_t *p_ri, SPOTNO spotno, HTTYPE httype);

/** Get the PCL attributes associated with the current render object. */
struct PclAttrib *pcl_attrib_from_ri(const render_info_t *p_ri) ;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
