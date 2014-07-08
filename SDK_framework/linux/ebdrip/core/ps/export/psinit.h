/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!export:psinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PostScript initialisation
 */

#ifndef __PSINIT_H__
#define __PSINIT_H__

struct core_init_fns ; /* from SWcore */

/** \brief Initialise the C runtime state for PostScript. */
void postscript_C_globals(struct core_init_fns *fns) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
