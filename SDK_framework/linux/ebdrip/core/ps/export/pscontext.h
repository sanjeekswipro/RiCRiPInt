/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!export:pscontext.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PostScript Interpreter contexts.
 */

#ifndef __PSCONTEXT_H__
#define __PSCONTEXT_H__

/** \brief PostScript interpreter context structure.
 */
struct ps_context_t {
  /** Parent reference to the RIP's per-thread context. */
  corecontext_t *corecontext ;

  /** \todo ajcd 2009-01-18: Decide what else to move here. */
} ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
