/** \file
 * \ingroup crypt
 *
 * $HopeName: COREcrypt!export:cryptinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list compound initialisation.
 */

#ifndef __CRYPTINIT_H__
#define __CRYPTINIT_H__

struct core_init_fns ;

/** \brief Initialise the C runtime state for the COREcrypt compound and
    sub-directories. */
void crypt_C_globals(struct core_init_fns *fns) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
