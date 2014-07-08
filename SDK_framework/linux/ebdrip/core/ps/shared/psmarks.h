/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!shared:psmarks.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Marking operator interface to rest of PostScript compound.
 */

#ifndef __PSMARKS_H__
#define __PSMARKS_H__

struct core_init_fns ; /* from SWcore */

/** \brief Initialise the C runtime state for the marking operators. */
void ps_marking_C_globals(struct core_init_fns *fns) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
