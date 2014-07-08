/** \file
 * \ingroup halftone
 *
 * $HopeName: SWv20!export:ht_module.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation and termination declarations for modular halftone system.
 */

#ifndef __HT_MODULES_H__
#define __HT_MODULES_H__

struct core_init_fns ; /* from SWcore */

/** C runtime initialisation for modular halftones. */
void htm_C_globals(struct core_init_fns *fns) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
