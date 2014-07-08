/** \file
 * \ingroup cff
 *
 * $HopeName: COREfonts!src:cff.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Compact font format definitions for use within COREfonts. The prototypes
 * in here are mostly used to tie together the VM restore, GC scanning, and
 * font cache purging functions of the COREfonts module.
 */

#ifndef __CFF_H__
#define __CFF_H__

struct core_init_fns ; /* from SWcore */

/** \defgroup cff Compact Font Format fonts
    \ingroup fonts */
/** \{ */

void cff_C_globals(struct core_init_fns *fns) ;

/** Clear the CFF cache (expect for items in use). */
void cff_cache_clear(void);


/** A hook for the VM system to remove fonts being restored away. */
void cff_restore(int32 savelevel) ;

/** \} */

/*
Log stripped */
#endif /* protection for multiple inclusion */
