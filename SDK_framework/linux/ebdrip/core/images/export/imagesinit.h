/** \file
 * \ingroup images
 *
 * $HopeName: COREimages!export:imagesinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list compound initialisation.
 */

#ifndef __IMAGESINIT_H__
#define __IMAGESINIT_H__

struct core_init_fns ; /* from SWcore */

/** \brief Initialise the C runtime state for the COREimages compound and
    sub-directories. */
void images_C_globals(struct core_init_fns *fns) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
